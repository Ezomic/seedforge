#include "ichor/world/forged_campaign.h"

#include "ichor/world/forged_phase_maps.h"

#include <worldforge/gen_version.h>

#include <algorithm>

namespace ichor {

CampaignStatus saveForgedWorld(const std::filesystem::path& root, std::string_view campaign, const ForgedWorldKey& key, const CachedForgedWorld& world,
    const worldforge::ForgedWorld* forged)
{
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const std::vector<std::uint8_t> bytes = encodeForgedWorldCache(key, world);
    std::vector<WorldBlob> blobs{{kForgedWorldBlob, bytes}};
    std::vector<std::uint8_t> phases;
    if (forged != nullptr) {
        phases = encodeForgedPhaseMaps(key, *forged);
        if (phases.empty()) {
            return CampaignStatus::CorruptWorld;
        }
        blobs.push_back({kForgedPhasesBlob, phases});
    }
    return Campaign::create(root, campaign, key.seed, key.genVersion, blobs);
}

LoadedCampaignWorld loadForgedWorld(const std::filesystem::path& root, std::string_view campaign)
{
    LoadedCampaignWorld loaded;
    CampaignStatus status = CampaignStatus::Ok;
    const std::optional<Campaign> opened = Campaign::open(root, campaign, &status);
    if (!opened) {
        loaded.status = status == CampaignStatus::Incomplete ? ForgedLoadStatus::Incomplete
            : status == CampaignStatus::CorruptWorld         ? ForgedLoadStatus::Corrupt
                                                             : ForgedLoadStatus::NoSuchCampaign;
        return loaded;
    }
    if (opened->manifest().genVersion > worldforge::kGenVersion) {
        loaded.status = ForgedLoadStatus::NewerGenVersion;
        return loaded;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = opened->readBlob(kForgedWorldBlob);
    if (!bytes) {
        return loaded;
    }
    ForgedWorldKey key;
    CachedForgedWorld world;
    if (!decodeStoredForgedWorld(*bytes, key, world) || key.seed != opened->manifest().worldSeed || key.genVersion != opened->manifest().genVersion) {
        return loaded;
    }
    const auto& files = opened->manifest().files;
    if (std::any_of(files.begin(), files.end(), [](const WorldFileInfo& file) { return file.name == kForgedPhasesBlob; })) {
        const std::optional<std::vector<std::uint8_t>> phases = opened->readBlob(kForgedPhasesBlob);
        if (!phases) {
            return loaded;
        }
        loaded.forged = decodeForgedPhaseMaps(*phases, key, world);
        if (!loaded.forged) {
            return loaded;
        }
    }
    loaded.status = ForgedLoadStatus::Ok;
    loaded.key = key;
    loaded.world = std::move(world);
    loaded.olderGenVersion = key.genVersion < worldforge::kGenVersion;
    return loaded;
}

}
