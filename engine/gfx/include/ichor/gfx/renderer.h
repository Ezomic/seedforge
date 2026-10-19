#pragma once

#include "ichor/gfx/render_view.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Rml {
class RenderInterface;
}

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

    // Water: one surface mesh per cell with any, positions relative to the cell origin given in
    // WaterDraw. A renderer that draws no water hands back kNoHandle.
    virtual std::uint32_t uploadWaterCell(const Mesh& surface) { (void)surface; return kNoHandle; }
    virtual void removeWaterCell(std::uint32_t cell) { (void)cell; }

    // Vegetation: the meshes of the kinds once (mesh i is kind i), then one set of
    // instances per cell, drawn instanced with one buffer per cell.
    // `unblendedKinds` has a bit for each kind whose base is not blended into the ground colour.
    virtual bool setVegetationMeshes(const SceneBundle& trees, std::uint32_t unblendedKinds = 0) = 0;
    virtual std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants) = 0;
    virtual void removeVegetationCell(std::uint32_t cell) = 0;

    // Rebuilds pipelines from the shader files on disk. Hot reload calls this
    // when a compiled shader changes; a renderer without shaders does nothing.
    virtual bool reloadShaders() { return true; }

    // How long the GPU spent on each pass of a recent frame, in milliseconds, in draw order. Empty until a frame has
    // finished, and always empty where there is no GPU (the null renderer). Presentation only.
    struct GpuPassTime {
        const char* name;
        double ms;
    };
    virtual const std::vector<GpuPassTime>& gpuPasses() const
    {
        static const std::vector<GpuPassTime> none;
        return none;
    }

    virtual void beginFrame() = 0;
    virtual void submit(const RenderView& view) = 0;
    virtual void endFrame() = 0;

    // One line on the debug overlay for this frame. The null renderer drops it.
    virtual void debugText(std::string line) = 0;

    // Called every frame while the debug UI is being built, so the editor can add its
    // windows. Renderers without a UI ignore it.
    virtual void setGuiHook(std::function<void()> hook) { (void)hook; }

    // Saves the next frame that is presented, exactly as the player would see it (the scene, the UI
    // and the overlays), to a PNG at this path. A renderer with no window ignores it, and
    // screenshotPending() then stays true: asked for, and never saved. The frame can also be skipped
    // (a minimised window, a lost swapchain), which leaves it pending too.
    virtual void requestScreenshot(std::string path) { (void)path; }
    virtual bool screenshotPending() const { return false; }

    // The game UI: the interface RmlUi draws through, null where there is no window. Each
    // frame `update` is called with the size of what is drawn into, outside any pass, so the
    // caller can resize and lay out its UI context (which may upload textures); then `render`
    // is called inside the open pass, under the debug overlay. Neither runs while the
    // drawable is empty, as when the window is minimised.
    virtual Rml::RenderInterface* uiRenderInterface() { return nullptr; }
    virtual void setUiHooks(std::function<void(std::uint32_t, std::uint32_t)> update, std::function<void()> render)
    {
        (void)update;
        (void)render;
    }
};

}
