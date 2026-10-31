#pragma once

#include "ichor/assets/bundle_types.h"
#include "ichor/world/heightfield.h"

namespace ichor {

// A stand-in for libworldforge phases 1 to 3: seeded value-noise terrain with
// a few octaves and a coastline, written in the same shape the real forge
// will produce. Deterministic by construction: integer hashing and float
// arithmetic only, no libm.
Heightfield forgeHeightfield(const WorldDesc& desc);

// The three splat layers the terrain shader blends: grass, rock, snow.
// Procedural so the engine has no texture dependency until art arrives.
SceneBundle forgeTerrainMaterials(std::uint64_t seed, std::uint32_t size = 256);

}
