#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"

#include <filesystem>
#include <vector>

namespace ichor {

// Bumped whenever computeHydrology could give a different answer for the same heights, so that a
// cache written by an older build is recomputed instead of trusted.
constexpr std::uint32_t kHydrologyCacheVersion = 1;

// The hydrology of a world as bytes: a header naming the world it belongs to (its description and
// the digest of its heights), the result itself, and a checksum of everything before it.
std::vector<std::uint8_t> encodeHydrologyCache(const Heightfield& heightfield, const Hydrology& hydrology);

// False, leaving `out` empty, unless the bytes are an intact cache for exactly this heightfield.
bool decodeHydrologyCache(const std::vector<std::uint8_t>& bytes, const Heightfield& heightfield, Hydrology& out);

// The same as computeHydrology, but read from `directory`/hydrology.ihyd when that holds an intact
// cache for this heightfield, and otherwise computed and written there. A cache that cannot be read
// or written costs only the time to compute.
Hydrology loadOrComputeHydrology(const std::filesystem::path& directory, const Heightfield& heightfield);

}
