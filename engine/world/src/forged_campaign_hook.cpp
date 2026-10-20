#include "ichor/world/forged_campaign_hook.h"

#include <format>

namespace ichor {

CampaignWorldPlan planCampaignWorld(const std::filesystem::path& root, std::string_view campaign)
{
    CampaignWorldPlan plan;
    if (!Campaign::validName(campaign)) {
        plan.message = std::format("'{}' is not a usable campaign name; use letters, digits, '-' and '_'", campaign);
        return plan;
    }
    LoadedCampaignWorld loaded = loadForgedWorld(root, campaign);
    switch (loaded.status) {
    case ForgedLoadStatus::NoSuchCampaign:
        plan.action = CampaignWorldAction::ForgeAndSave;
        return plan;
    case ForgedLoadStatus::Incomplete:
        plan.message = std::format("campaign '{}' was interrupted while its world was being saved and cannot be loaded; delete its folder to start it again", campaign);
        return plan;
    case ForgedLoadStatus::Corrupt:
        plan.message = std::format("campaign '{}' has a damaged world and cannot be loaded; nothing was changed", campaign);
        return plan;
    case ForgedLoadStatus::NewerGenVersion:
        plan.message = std::format("campaign '{}' was made by a newer version of the generator than this build; update the game to open it", campaign);
        return plan;
    case ForgedLoadStatus::Ok:
        break;
    }
    if (loaded.key.gridSize < 16 || loaded.key.gridSize > 1024) {
        plan.message = std::format("campaign '{}' stores a world grid of {} cells, which this build cannot forge", campaign, loaded.key.gridSize);
        return plan;
    }
    plan.action = CampaignWorldAction::Rebuild;
    plan.warn = loaded.olderGenVersion;
    plan.key = loaded.key;
    plan.stored = std::move(loaded.world);
    plan.message = loaded.olderGenVersion
        ? std::format("campaign '{}' was made by an older generator (version {}); loading its stored world as it is", campaign, loaded.key.genVersion)
        : std::format("loading campaign '{}' (seed {}, {} cell grid)", campaign, loaded.key.seed, loaded.key.gridSize);
    return plan;
}

std::string verifyRebuiltWorld(const CampaignWorldPlan& plan, const ForgedGame& game)
{
    if (!game.world) {
        return "the world was not forged";
    }
    if (game.world->worldHash() != plan.stored.worldHash) {
        return std::format("this generator forges world {:016x} from the stored seed and grid, not the stored {:016x}; the campaign's world cannot be rebuilt", game.world->worldHash(), plan.stored.worldHash);
    }
    if (game.heightfield.samples() != plan.stored.heightfield.samples()) {
        return "the rebuilt terrain differs from the stored terrain; the campaign's world cannot be rebuilt";
    }
    return {};
}

CampaignStatus saveCampaignWorld(const std::filesystem::path& root, std::string_view campaign, const WorldDesc& requested, int gridSize, const ForgedGame& game)
{
    if (!game.world || !game.world->manifest().has_value()) {
        return CampaignStatus::CorruptWorld;
    }
    CachedForgedWorld world;
    world.worldHash = game.world->worldHash();
    world.heightfield = game.heightfield;
    world.manifest = *game.world->manifest();
    return saveForgedWorld(root, campaign, forgedWorldKey(requested, gridSize), world);
}

}
