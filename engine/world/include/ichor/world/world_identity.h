#pragma once

#include "ichor/world/world_desc.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ichor {

// The generator version this build forges with. It moves whenever forge output changes for a given seed.
inline constexpr std::uint32_t kForgeGenVersion = 1;

// What it takes to name a world: the seed it grows from, the generator version that grew it and a
// digest of what came out. Two players holding equal identities hold the same terrain, so terrain
// is never sent over the wire.
struct WorldIdentity {
    std::uint64_t seed = 0;
    std::uint32_t genVersion = 0;
    std::uint64_t contentDigest = 0;

    bool operator==(const WorldIdentity&) const = default;
};

// "ichor1.<seed:16 hex>.<genVersion:8 hex>.<digest:16 hex>.<check:4 hex>", lowercase. The check
// catches a mistyped or truncated string before any forging starts.
std::string toString(const WorldIdentity& identity);

// Nullopt for anything toString could not have produced.
std::optional<WorldIdentity> parseWorldIdentity(std::string_view text);

// Digest of the forged world: the heightfield samples plus the shape and ranges they are read with.
// `desc` must be valid.
std::uint64_t worldContentDigest(const WorldDesc& desc);

// Forges `desc` and names the result under `genVersion`. Nullopt when `desc` cannot be forged.
std::optional<WorldIdentity> identifyWorld(const WorldDesc& desc, std::uint32_t genVersion = kForgeGenVersion);

enum class WorldMatch {
    Match,
    InvalidShape,       // `shape` is not a forgeable world
    UnsupportedVersion, // the identity was made by another generator version than this build forges
    DigestMismatch,     // same seed and version, but the forged content differs
};

// Forges a world from the identity's seed (everything else about the shape comes from `shape`) and
// checks that its digest is the identity's.
WorldMatch verifyWorld(const WorldIdentity& identity, const WorldDesc& shape);

}
