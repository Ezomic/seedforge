#pragma once

#include "ichor/gfx/render_view.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ichor {

struct Mesh;
struct SceneBundle;

class Renderer {
public:
    static constexpr std::uint32_t kNoHandle = 0xffffffffu;

    virtual ~Renderer() = default;

    virtual bool init() = 0;

    // Makes a cooked bundle's meshes, textures and materials drawable, after
    // whatever was uploaded before: the bundle's meshes get indices starting
    // at meshCount() as it was before the call.
    virtual bool uploadBundle(const SceneBundle& bundle) = 0;
    virtual std::uint32_t meshCount() const = 0;

    // Terrain: the three splat layers once, then cells as they stream in and
    // out. A cell is one mesh per level of detail, positions relative to the
    // cell origin given in TerrainDraw.
    virtual bool setTerrainMaterials(const SceneBundle& layers) = 0;
    virtual std::uint32_t uploadTerrainCell(const std::vector<Mesh>& lods) = 0;
    virtual void removeTerrainCell(std::uint32_t cell) = 0;

    // Vegetation: the meshes of the kinds once (mesh i is kind i), then one set of
    // instances per cell, drawn instanced with one buffer per cell.
    virtual bool setVegetationMeshes(const SceneBundle& trees) = 0;
    virtual std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants) = 0;
    virtual void removeVegetationCell(std::uint32_t cell) = 0;

    // Rebuilds pipelines from the shader files on disk. Hot reload calls this
    // when a compiled shader changes; a renderer without shaders does nothing.
    virtual bool reloadShaders() { return true; }

    virtual void beginFrame() = 0;
    virtual void submit(const RenderView& view) = 0;
    virtual void endFrame() = 0;

    // One line on the debug overlay for this frame. The null renderer drops it.
    virtual void debugText(std::string line) = 0;

    // Called every frame while the debug UI is being built, so the editor can add its
    // windows. Renderers without a UI ignore it.
    virtual void setGuiHook(std::function<void()> hook) { (void)hook; }
};

}
