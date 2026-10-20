#pragma once

#include "ichor/world/forged_campaign.h"
#include "ichor/world/forged_game.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ichor {

// What `ichor --forged --campaign NAME` does with a campaign's saved world (doc 14). The stored file keeps the
// heightfield and the content manifest but not the phase maps (biomes, water, roads), so the game around it is
// still rebuilt by running the forge; the stored world is what that rebuild is held to. A rebuild that does not
// reproduce the stored world hash and terrain is an error, never a silent substitution.
enum class CampaignWorldAction {
    ForgeAndSave, // no such campaign: forge the requested world, then save it
    Rebuild,      // the campaign exists and loads: forge the stored seed and grid, then check it against `stored`
    Refuse,       // the campaign exists and cannot be used: say why, forge nothing, overwrite nothing
};

struct CampaignWorldPlan {
    CampaignWorldAction action = CampaignWorldAction::Refuse;
    ForgedWorldKey key;        // Rebuild: the key the world was stored under (seed and grid to forge)
    CachedForgedWorld stored;  // Rebuild
    bool warn = false;         // Rebuild: the stored genVersion is older than this build's
    std::string message;       // what to tell the player; empty for ForgeAndSave
};

// Never throws or crashes on any bytes on disk.
CampaignWorldPlan planCampaignWorld(const std::filesystem::path& root, std::string_view campaign);

// Empty when `game` is the stored world, else why it is not.
std::string verifyRebuiltWorld(const CampaignWorldPlan& plan, const ForgedGame& game);

// Saves the forged `game` as campaign `campaign`. `requested` is the WorldDesc it was forged from.
CampaignStatus saveCampaignWorld(const std::filesystem::path& root, std::string_view campaign, const WorldDesc& requested, int gridSize, const ForgedGame& game);

}
