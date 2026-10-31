#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"

namespace ichor {

struct WaterMeshParams {
    float riverSurface = 0.35f; // metres a river's water lies over the bed beneath it
    float submerge = 0.6f;      // metres below the ground that the surface is sunk where there is no water
};

// Where the water surface lies at one sample of the world, shared by the meshes and by
// WaterField so that what is drawn and what is waded through cannot drift apart. A sample with
// water has its lake's filled level or a river's bed plus riverSurface. A dry one is sunk below its
// own ground and below the water beside it, so the surface passes under a bank lower than the water.
struct WaterLevel {
    bool wet = false;
    float level = 0.0f;
};
WaterLevel waterLevelAtSample(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, const WaterMeshParams& params, std::int64_t gx, std::int64_t gz);

// Which way the water runs at one sample, on the ground plane (x, z): the mean of the unit downstream
// directions of the rivers whose water covers the sample, so its length is at most one, and less where
// rivers part or meet. Zero for a lake, for dry ground no river reaches, for a river's outlet, and
// for a sample outside the world. The shader slides its ripples along it.
glm::vec2 riverFlowAtSample(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, std::int64_t gx, std::int64_t gz);

// The water of one cell as a surface mesh, positions relative to the cell origin like the
// terrain's. Lakes lie at the level the flood filled them to, rivers a little over the channel
// bed. Every quad with water at any corner is drawn; its dry corners are sunk below the ground,
// so the surface ends where it meets the bank, at the true shoreline and not on the sample grid.
// Each vertex carries in uv.x how far the surface stands over the ground there, zero or less at the
// waterline and beyond, in uv.y 1 where water lies on all eight sides of the sample and 0 at the water's edge and beyond (the shader ramps the shore from it), and in tangent the flow: xz a unit direction (x axis where there is none) and w
// how much of it there is, the length of riverFlowAtSample, zero on lakes. A cell with no water comes back with no vertices. The hydrology must be of this heightfield,
// or the result is empty. A pure function of the ground, so a cell built twice is the same, and
// neighbours agree along their shared edge.
Mesh buildWaterMesh(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, CellId id, const WaterMeshParams& params = {});

}
