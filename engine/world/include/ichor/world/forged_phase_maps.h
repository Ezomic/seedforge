#pragma once

#include "ichor/world/forged_world_cache.h"

#include <worldforge/forge_world.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ichor {

// The phase maps the bake reads (hydrology, biomes, settlements, roads, politics, POIs), stored in a campaign's world/
// folder beside the forged world file (doc 14), so a load rebuilds the game from the stored files and never runs the
// forge. Tectonics, erosion, climate and resources feed only the phases after them and the bake reads none of them, so
// they are not stored; their phase hashes are, which keeps the world hash checkable.
//
// Layout (little endian, version 1): magic "IFWP", version, seed, genVersion, grid size, world hash, the eleven phase
// hashes, the six maps (every per-cell array has size * size entries), the landmark coverage report (the POI phase hash
// folds it in), and a trailing hash of everything before it. The content manifest is not repeated: it lives in the
// forged world file and is read from there.
//
// A decoded file is trusted only after it passes: the checksum, the stamp matching the forged world file and every map
// structurally sound (sizes, indices, enum ranges). A file of this build's genVersion must also hash right: each map
// to its phase hash, the manifest to phase 11 and the phase hashes chained to the world hash. A world made by an older
// generator is not held to this build's hash functions, which may have changed since, and loads as stored.

constexpr const char* kForgedPhasesBlob = "forged_phases.bin";

// Empty unless `world` is complete with all six maps, the coverage report and the manifest, and `key` names its seed and grid.
std::vector<std::uint8_t> encodeForgedPhaseMaps(const ForgedWorldKey& key, const worldforge::ForgedWorld& world);

// Never throws or crashes on any input. Null unless `bytes` is an intact phase file for exactly this key and for the
// world `stored` (hash and manifest). The returned world reports complete() and has the six maps and the manifest.
std::shared_ptr<const worldforge::ForgedWorld> decodeForgedPhaseMaps(std::span<const std::uint8_t> bytes, const ForgedWorldKey& key, const CachedForgedWorld& stored);

// The world hash a run with these phase hashes has under `genVersion`; chainForgeHash with the version a parameter.
std::uint64_t chainForgeHashAt(std::uint64_t seed, std::uint32_t genVersion, int size, const std::array<std::uint64_t, worldforge::kForgePhaseCount>& phaseHashes,
    std::array<std::uint64_t, worldforge::kForgePhaseCount>* chain = nullptr);

}
