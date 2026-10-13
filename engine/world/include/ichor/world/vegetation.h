#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/gfx/render_view.h"
#include "ichor/world/heightfield.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace ichor {

// One planted tree. Positions are relative to the cell origin, like the cell's
// meshes, so the renderer places a whole cell with one translation.
using VegetationInstance = PlantInstance;

struct VegetationParams {
    float spacing = 14.0f;    // metres between the jittered grid points that candidates come from
    float maxSlope = 0.3f;    // 1 minus the surface normal's up component; steeper ground stays bare
    float minHeight = 2.0f;   // metres above the height range's floor
    float maxHeight = 220.0f; // above this is the snow line
    float forestScale = 600.0f; // metres across a forest patch or a clearing
    float bankMargin = 1.0f;  // metres above the world's water that trees keep clear of
};

// Kinds of vegetation, in the order of the meshes in treeBundle().
enum class VegetationKind : std::uint32_t { Conifer = 0, Broadleaf = 1, Count };

// Plants a cell. The result depends only on the world seed, the cell and the
// parameters, never on the order cells are asked for in, so a cell streamed in
// twice grows the same trees and neighbours need not be loaded.
void scatterVegetation(const Heightfield& heightfield, CellId id, const VegetationParams& params, std::vector<VegetationInstance>& out);

// The tree meshes and their materials, built in code: a conifer of stacked cones
// and a broadleaf crown, each over a trunk. Mesh index i is VegetationKind i.
SceneBundle treeBundle();

}
