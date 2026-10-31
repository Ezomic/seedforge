#include "ichor/gfx/null_renderer.h"

#include "ichor/assets/bundle.h"

namespace ichor {

bool NullRenderer::uploadBundle(const SceneBundle& bundle)
{
    std::vector<std::uint32_t> evicted;
    std::vector<std::uint32_t> ids;
    for (const Texture& texture : bundle.textures) {
        const std::uint32_t tailStart = mipTailStart(texture.width, texture.height, texture.mipCount);
        ids.push_back(residency_.add(texture.pixels.size(), mipRangeByteSize(texture.format, texture.width, texture.height, texture.mipCount, tailStart), evicted));
    }
    for (const Mesh& mesh : bundle.meshes) {
        std::vector<std::uint32_t> textures;
        for (const Submesh& sub : mesh.submeshes) {
            if (sub.material >= bundle.materials.size()) {
                continue;
            }
            const Material& material = bundle.materials[sub.material];
            for (const std::uint32_t local : {material.baseColorTexture, material.metallicRoughnessTexture, material.normalTexture}) {
                if (local < ids.size()) {
                    textures.push_back(ids[local]);
                }
            }
        }
        meshTextures_.push_back(std::move(textures));
    }
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
    residency_.beginFrame();
}

void NullRenderer::submit(const RenderView& view)
{
    instances_ += view.instances.size();
    std::vector<std::uint32_t> evicted;
    for (const RenderInstance& instance : view.instances) {
        skinned_ += instance.jointCount > 0 ? 1 : 0;
        if (instance.mesh < meshTextures_.size()) {
            for (const std::uint32_t texture : meshTextures_[instance.mesh]) {
                residency_.touch(texture, evicted);
            }
        }
    }
    terrainDraws_ += view.terrain.size();
    vegetationDraws_ += view.vegetation.size();
    waterDraws_ += view.waterCells.size();
    lastDraws_ = view.terrain.size() + view.vegetation.size() + view.waterCells.size() + view.instances.size();
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
