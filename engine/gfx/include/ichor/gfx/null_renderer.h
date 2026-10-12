#pragma once

#include "ichor/gfx/renderer.h"

#include <cstdint>
#include <set>

namespace ichor {

class NullRenderer final : public Renderer {
public:
    bool init() override { return true; }
    bool uploadBundle(const SceneBundle& bundle) override;
    std::uint32_t meshCount() const override { return meshes_; }
    bool setTerrainMaterials(const SceneBundle& layers) override;
    std::uint32_t uploadTerrainCell(const std::vector<Mesh>& lods) override;
    void removeTerrainCell(std::uint32_t cell) override;
    bool setVegetationMeshes(const SceneBundle& trees) override;
    std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants) override;
    void removeVegetationCell(std::uint32_t cell) override;
    void beginFrame() override;
    void submit(const RenderView& view) override;
    void endFrame() override;
    void debugText(std::string line) override;

    std::uint64_t framesBegun() const { return begun_; }
    std::uint64_t framesEnded() const { return ended_; }
    std::uint64_t linesDropped() const { return lines_; }
    std::uint64_t instancesSubmitted() const { return instances_; }
    std::uint64_t skinnedInstancesSubmitted() const { return skinned_; }
    std::uint64_t terrainDrawsSubmitted() const { return terrainDraws_; }
    std::uint32_t meshesUploaded() const { return meshes_; }
    std::size_t terrainCellsHeld() const { return cells_.size(); }
    std::size_t vegetationCellsHeld() const { return plantCells_.size(); }
    std::uint64_t vegetationDrawsSubmitted() const { return vegetationDraws_; }

private:
    std::uint64_t begun_ = 0;
    std::uint64_t ended_ = 0;
    std::uint64_t lines_ = 0;
    std::uint64_t instances_ = 0;
    std::uint64_t skinned_ = 0;
    std::uint64_t terrainDraws_ = 0;
    std::uint32_t meshes_ = 0;
    std::uint32_t nextCell_ = 0;
    std::set<std::uint32_t> cells_;
    std::set<std::uint32_t> plantCells_;
    std::uint32_t nextPlantCell_ = 0;
    std::uint64_t vegetationDraws_ = 0;
};

}
