#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"

#include <worldforge/forge_world.h>

#include <memory>
#include <string>

namespace ichor {

// The bake adapter for water (doc 08 phase 12): turns the rivers and lakes libworldforge forged
// (HydrologyMap river mask, lake depth, filled height, flow and catchment) into the engine's
// Hydrology, so WaterField and the water meshes draw the forged world's water and not the water the
// engine would find again by flooding the adapted terrain.
//
// The engine Hydrology lives on the Heightfield's sample grid and the forged maps on a coarser cell
// grid, so every sample takes the discrete facts (river, lake, flow direction, catchment) of the
// forged cell nearest to it, chosen in integer arithmetic. A river is therefore as wide as a forged
// cell, drawn as blocks of samples, and widens further by the engine's own riverHalfWidth rule.
// Where a sample is not a lake its filled level is its own ground, so no lake appears that the forge
// did not make. A lake sample lies at its forged cell's filled level (flat across the lake), or at
// its ground where that is higher, which is what rounds the shore.
//
// Pair the Hydrology with `rivers`, not with the engine's defaults: the river threshold is the
// smallest catchment of any forged river cell (at least 2, dry samples count 1), so exactly the forged river cells are rivers.
struct ForgedWater {
    std::shared_ptr<const Hydrology> hydrology;
    HydrologyParams rivers;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// `heightfield` should be the adapter's (heightfieldFromForgedWorld) for the same world, because the
// water is only as right as the ground under it. Fails, with a message, when the heightfield is
// empty or too small for a Hydrology, or when the world did not complete phase 3 or is not a square
// grid of at least 2 cells a side. Nothing throws.
ForgedWater waterFromForgedWorld(const worldforge::ForgedWorld& world, const Heightfield& heightfield);

}
