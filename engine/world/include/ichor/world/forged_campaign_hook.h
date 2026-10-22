#pragma once

#include "ichor/world/forged_campaign.h"
#include "ichor/world/forged_game.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ichor {

// What `ichor --forged --campaign NAME` does with a campaign's saved world (doc 14). A campaign saved with its phase
// maps (forged_phases.bin) is loaded: the game is built from the stored files alone and the forge is not run. One saved
// without them (made before the maps were stored) keeps the heightfield and the content manifest only, so the game
// around it is rebuilt by running the forge and the stored world is what that rebuild is held to. A rebuild that does
// not reproduce the stored world hash and terrain is an error, never a silent substitution.
enum class CampaignWorldAction {
    ForgeAndSave, // no such campaign: forge the requested world, then save it
    Load,         // the campaign exists with its phase maps: build the game from `forged`, no forge
    Rebuild,      // the campaign exists without them: forge the stored seed and grid, then check it against `stored`
    Refuse,       // the campaign exists and cannot be used: say why, forge nothing, overwrite nothing
};

struct CampaignWorldPlan {
    CampaignWorldAction action = CampaignWorldAction::Refuse;
    ForgedWorldKey key;        // Rebuild: the key the world was stored under (seed and grid to forge)
    CachedForgedWorld stored;  // Load, Rebuild
    std::shared_ptr<const worldforge::ForgedWorld> forged; // Load: the world rebuilt from the stored phase maps
    bool warn = false;         // Load, Rebuild: the stored genVersion is older than this build's
    std::string message;       // what to tell the player; empty for ForgeAndSave
};

// Never throws or crashes on any bytes on disk.
CampaignWorldPlan planCampaignWorld(const std::filesystem::path& root, std::string_view campaign);

// Builds the game of a Load plan from its stored world, with no forge, and holds it to the stored hash, manifest and
// terrain. The game's error is set (so ok() is false) when the stored world does not make a consistent game.
std::unique_ptr<ForgedGame> buildStoredCampaignGame(const CampaignWorldPlan& plan, const WorldDesc& requested, const ForgedContentInputs& content = {});

// Empty when `game` is the stored world, else why it is not.
std::string verifyRebuiltWorld(const CampaignWorldPlan& plan, const ForgedGame& game);

const char* campaignStatusText(CampaignStatus status);

// Saves the forged `game` as campaign `campaign`. `requested` is the WorldDesc it was forged from.
CampaignStatus saveCampaignWorld(const std::filesystem::path& root, std::string_view campaign, const WorldDesc& requested, int gridSize, const ForgedGame& game);

}
