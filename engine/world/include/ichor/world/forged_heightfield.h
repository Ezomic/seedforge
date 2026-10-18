#pragma once

#include "ichor/world/heightfield.h"

#include <worldforge/forge_world.h>

#include <string>

namespace ichor {

// The bake adapter (doc 08 phase 12, first step): turns the terrain libworldforge forged into the
// engine's Heightfield. It is the one place the engine reads a worldforge::ForgedWorld for height, so
// the bake tickets that follow take their terrain from here.
//
// The source is HydrologyMap::heightM, the eroded terrain with river channels carved in (phase 3).
// It is a square grid of ForgedWorld::size() cells per side; the first and last cell sit on the
// world's edges. The Heightfield has desc.samplesPerSide() samples per side, so each sample is a
// bilinear blend of the four forged cells around it, in integer arithmetic and one correctly rounded
// double division: no libm, the same samples on every platform.
//
// Units: a forged height is whole metres. A sample is (metres - desc.minHeight) / desc.heightRange of
// 65535, rounded to nearest and clamped to [0, 65535], so heights outside the desc's range flatten
// against its ends. Pick the range with forgedWorldDesc, or set your own.

// The forge's sea level in metres (HydrologyParams::seaLevelM defaults to 0).
constexpr int kForgedSeaLevelM = 0;

// Forged grid cells per side when none is asked for (worldforge::kForgeDefaultSize).
constexpr int kForgedDefaultGridSize = worldforge::kForgeDefaultSize;

// A result that is either a Heightfield or the reason there is none. Nothing throws and nothing
// aborts on a bad desc or world.
struct ForgedHeightfield {
    Heightfield field;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// `base` with the height range worldforge can produce (-6000 to 8000 m) and the water at the forged
// sea level, so the carved rivers and the coast line up with desc.waterLevel(). The range costs
// about 0.2 m per sample unit. Everything else in `base` is kept.
WorldDesc forgedWorldDesc(WorldDesc base);

// Adapts a forged world. Fails, with a message, when `desc` is not valid or has a non-finite height
// range, when the world did not complete phase 3 (hydrology), or when its grid is not square and
// at least 2 cells a side.
ForgedHeightfield heightfieldFromForgedWorld(const worldforge::ForgedWorld& world, const WorldDesc& desc);

// Forges phases 1 to 3 with desc.seed on a gridSize x gridSize grid and adapts the result. gridSize
// must lie in worldforge's limits (16 to 1024, within the erosion budget); a world it refuses comes
// back as an error. The default keeps the forge fast; the sample grid is finer than the forged one.
ForgedHeightfield forgeHeightfieldFromWorldforge(const WorldDesc& desc, int gridSize = kForgedDefaultGridSize);

}
