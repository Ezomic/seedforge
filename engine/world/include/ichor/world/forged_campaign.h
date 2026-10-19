#pragma once

#include "ichor/save/campaign.h"
#include "ichor/world/forged_world_cache.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace ichor {

// The forged world saved into a campaign's world/ folder (doc 14), so a loaded game gets the world it
// was made with and never the one today's generator would forge. The folder holds one blob, the
// forged world file (seed, genVersion, grid size, forge config hash, desc hash, world hash, the baked
// heightfield and the content manifest, which is what the bake reads), beside the campaign manifest
// that names its size and hash and carries the seed and genVersion.
//
// The phase maps stay in memory only, so a bake that needs more than the heightfield and manifest
// still forges.
//
// Migration policy: the world is baked, so a stored world is loaded as it is whatever genVersion made
// it. An older genVersion loads and is reported (olderGenVersion), because a patched generator must
// not break an existing campaign. A genVersion newer than this build's is refused (NewerGenVersion),
// because this build cannot know what the newer generator's data means.

constexpr const char* kForgedWorldBlob = "forged_world.bin";

enum class ForgedLoadStatus {
    Ok,
    NoSuchCampaign,
    Incomplete,      // the forge was interrupted: the world folder has no manifest
    Corrupt,         // a damaged manifest or blob, or the two disagree about seed or genVersion
    NewerGenVersion, // made by a newer generator than this build; refused
};

struct LoadedCampaignWorld {
    ForgedLoadStatus status = ForgedLoadStatus::Corrupt;
    ForgedWorldKey key; // the key the world was stored under
    CachedForgedWorld world;
    bool olderGenVersion = false; // the stored genVersion is older than this build's; the world is still the stored one

    bool ok() const { return status == ForgedLoadStatus::Ok; }
};

// Writes the campaign. Refuses an existing campaign and leaves nothing behind on failure.
CampaignStatus saveForgedWorld(const std::filesystem::path& root, std::string_view campaign, const ForgedWorldKey& key, const CachedForgedWorld& world);

// Never throws or crashes on any bytes on disk.
LoadedCampaignWorld loadForgedWorld(const std::filesystem::path& root, std::string_view campaign);

// The key stamped in a forged world file, without checking the rest of it. Nullopt when the bytes are
// too short or are not a forged world file; the full decode is what proves the file intact.
std::optional<ForgedWorldKey> peekForgedWorldKey(const std::vector<std::uint8_t>& bytes);

// Decodes a forged world file under the key it was stamped with rather than one derived from today's
// defaults. False, leaving `out` empty, unless the file is intact.
bool decodeStoredForgedWorld(const std::vector<std::uint8_t>& bytes, ForgedWorldKey& key, CachedForgedWorld& out);

}
