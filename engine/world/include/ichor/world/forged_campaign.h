#pragma once

#include "ichor/save/campaign.h"
#include "ichor/world/forged_world_cache.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ichor {

// The forged world saved into a campaign's world/ folder (doc 14), so a loaded game gets the world it
// was made with and never the one today's generator would forge. The folder holds the
// forged world file (seed, genVersion, grid size, forge config hash, desc hash, world hash, the baked
// heightfield and the content manifest, which is what the bake reads), beside the campaign manifest
// that names its size and hash and carries the seed and genVersion.
//
// A second blob (forged_phases.bin, forged_phase_maps.h) holds the phase maps the bake reads, so a load
// builds the game from the stored files and never forges. A campaign saved before that blob existed has
// only the first one (forged == null on load) and is rebuilt by forging, as it always was.
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
    std::shared_ptr<const worldforge::ForgedWorld> forged; // from the stored phase maps; null for a campaign saved without them
    bool olderGenVersion = false; // the stored genVersion is older than this build's; the world is still the stored one

    bool ok() const { return status == ForgedLoadStatus::Ok; }
};

// Writes the campaign. Refuses an existing campaign and leaves nothing behind on failure. With `forged`, the phase
// maps are stored too (CorruptWorld when it lacks them); without it the campaign is stored as before.
CampaignStatus saveForgedWorld(const std::filesystem::path& root, std::string_view campaign, const ForgedWorldKey& key, const CachedForgedWorld& world,
    const worldforge::ForgedWorld* forged = nullptr);

// Never throws or crashes on any bytes on disk.
LoadedCampaignWorld loadForgedWorld(const std::filesystem::path& root, std::string_view campaign);

// The key stamped in a forged world file, without checking the rest of it. Nullopt when the bytes are
// too short or are not a forged world file; the full decode is what proves the file intact.
std::optional<ForgedWorldKey> peekForgedWorldKey(const std::vector<std::uint8_t>& bytes);

// Decodes a forged world file under the key it was stamped with rather than one derived from today's
// defaults. False, leaving `out` empty, unless the file is intact.
bool decodeStoredForgedWorld(const std::vector<std::uint8_t>& bytes, ForgedWorldKey& key, CachedForgedWorld& out);

}
