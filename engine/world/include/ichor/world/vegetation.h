#pragma once

#include "ichor/assets/bundle_types.h"
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
    float forestDensity = 1.0f; // scales how much of the forest pattern grows trees; 0 is bare, 1 the full pattern
    float rockSpacing = 9.0f;    // metres between the jittered grid points that rock candidates come from
    float rockChance = 0.07f;    // the chance a candidate on level ground holds a rock
    float rockSteepBoost = 0.55f; // added chance on steep ground, where rock shows through
    float bankMargin = 1.0f;  // metres above the world's water that trees keep clear of
};

// Kinds of vegetation, in the order of the meshes in treeBundle().
enum class VegetationKind : std::uint32_t { Conifer = 0, Broadleaf = 1, Rock = 2, Grass = 3, Bush = 4, DeadTree = 5, Cactus = 6, DryShrub = 7, Reed = 8, Count };

// The smooth patchwork of forest and clearing that the tree scatter follows, 0 in a clearing to 1 in the
// thick of a forest. The ground cover follows it too, so grass and trees clump in the same places.
float forestPatch(double x, double z, float scale);

// The kinds whose bases are not blended into the ground colour when drawn: a rock is not a plant
// rooted in the grass. A bit per kind, for the renderer.
constexpr std::uint32_t vegetationUnblendedKinds() { return 1u << static_cast<std::uint32_t>(VegetationKind::Rock); }

// Plants a cell: trees first, then rocks. The trees come out as they always did, whether or not
// rocks are wanted, because each candidate has a generator of its own. The result depends only on the world seed, the cell and the
// parameters, never on the order cells are asked for in, so a cell streamed in
// twice grows the same trees and neighbours need not be loaded.
void scatterVegetation(const Heightfield& heightfield, CellId id, const VegetationParams& params, std::vector<VegetationInstance>& out);

// The meshes and their materials, built in code: a conifer of stacked cones and a broadleaf
// crown, each over a trunk, a half-buried boulder, and the small ground cover and bare trees of the
// forged biomes (grass, bush, dead tree, cactus, dry shrub, reed). Mesh index i is VegetationKind i.
SceneBundle treeBundle();

}
