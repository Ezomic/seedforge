#pragma once

#include "ichor/world/forged_heightfield.h"
#include "ichor/world/heightfield.h"

#include <worldforge/content_manifest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// The forged world on disk (doc 08 phase 12): what the engine takes from a forge, written once so a
// reload of the same world skips the forge. Today that is the baked heightfield, the content manifest
// and the forge's world hash; the phase maps stay in memory only, and a bake step that needs them still
// forges.
//
// An entry is named by every input that changes the answer: the world seed, the generator version, the
// forged grid size, a hash of the forge config and a hash of the WorldDesc the heightfield was baked
// with. Change any of them and the old file is simply not found; a stale or damaged file that is found
// is rejected, never trusted, and the world is forged again.

// Names the ForgeConfig a forge ran with. The new-game flow forges with the default config, which is 0.
constexpr std::uint64_t kDefaultForgeConfigHash = 0;

struct ForgedWorldKey {
    std::uint64_t seed = 0;
    std::uint32_t genVersion = 0;
    std::int32_t gridSize = 0;
    std::uint64_t configHash = kDefaultForgeConfigHash;
    std::uint64_t descHash = 0; // of the baked heightfield's WorldDesc

    bool operator==(const ForgedWorldKey&) const = default;
};

// The key for forging `desc` on a gridSize grid. The desc is passed through forgedWorldDesc, as the
// forge job does, and genVersion is the linked libworldforge's. The config hash is the default config's; a
// caller that forges with another ForgeConfig sets the field itself.
ForgedWorldKey forgedWorldKey(const WorldDesc& desc, int gridSize = kForgedDefaultGridSize);

struct CachedForgedWorld {
    std::uint64_t worldHash = 0;
    Heightfield heightfield;
    worldforge::ContentManifest manifest;
};

std::vector<std::uint8_t> encodeForgedWorldCache(const ForgedWorldKey& key, const CachedForgedWorld& world);
// Never throws or crashes on any input. False, leaving `out` empty, unless the bytes are an intact
// cache entry for exactly this key.
bool decodeForgedWorldCache(const std::vector<std::uint8_t>& bytes, const ForgedWorldKey& key, CachedForgedWorld& out);

std::filesystem::path forgedWorldCachePath(const std::filesystem::path& directory, const ForgedWorldKey& key);

// Reads the entry for `key` from `directory`; empty when there is none or it fails validation.
std::optional<CachedForgedWorld> readForgedWorldCache(const std::filesystem::path& directory, const ForgedWorldKey& key);
bool writeForgedWorldCache(const std::filesystem::path& directory, const ForgedWorldKey& key, const CachedForgedWorld& world);

struct LoadedForgedWorld {
    CachedForgedWorld world;
    bool fromCache = false;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// The cached world when `directory` holds an intact entry for this seed, grid and config, otherwise a
// fresh forge (default ForgeConfig, phases 1 to 11) written there. A cache that cannot be written costs
// only the forge next time. Blocks for the whole forge, so call it from a worker thread.
LoadedForgedWorld loadOrForgeWorld(const std::filesystem::path& directory, const WorldDesc& desc, int gridSize = kForgedDefaultGridSize);

}
