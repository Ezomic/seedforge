#pragma once

#include "ichor/world/encounter_budget.h"
#include "ichor/world/world_desc.h"

#include <worldforge/forge_world.h>
#include <worldforge/region_tier_assign.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The bake step that connects the forged region tiers (worldforge region_tier_assign.h, doc 04) to
// the encounter budget (doc 05). A forged cell is a region: its tier comes from how far it is from
// the start and, when the world has politics, from whether a rival faction contests it. The lookup
// takes world metres, so the encounter code never sees the forged grid.

struct ForgedTierParams {
    // ringWidth is in metres of world distance.
    worldforge::RegionTierParams rings{.tierCount = 5, .ringWidth = 400.0, .ringJitter = 0.3, .dangerWeight = 0.5};
    // The start, in forged grid cells. Negative picks the centre of the grid.
    int startCellX = -1;
    int startCellY = -1;
};

struct ForgedRegionTiers {
    std::string error; // empty on success
    std::uint32_t tierCount = 0;
    int gridSize = 0;
    std::uint32_t sizeMetres = 0;
    std::vector<std::uint8_t> tiers; // gridSize * gridSize, row major

    bool ok() const { return error.empty(); }

    // The tier of the region under a world position (x, z in metres). Positions off the world clamp to
    // its edge. A NaN or infinite position counts as unreachable: the top tier. Tier 0 when not ok().
    std::uint8_t tierAt(double worldX, double worldZ) const;
};

// Assigns every cell of `world` a tier. Needs the world to have completed hydrology (the grid) and
// valid params; danger is read from politics when that phase ran, else it is 0.
ForgedRegionTiers forgedRegionTiers(const worldforge::ForgedWorld& world, const WorldDesc& desc, const ForgedTierParams& params = {});

struct TierEncounterParams {
    std::uint32_t baseBudget = 6;
    std::uint32_t budgetPerTier = 4;
    std::uint32_t maxBudget = 64;
    // Weight added per tier, in permille of the role's own, for the heavy roles (Anchor, Artillery,
    // Controller), so higher tiers draw tougher enemies more often.
    std::uint32_t heavyWeightPermillePerTier = 500;
};

// Problems with the tuning (a zero base budget, a cap below the base). Empty when usable.
std::vector<std::string> validateTierEncounterParams(const TierEncounterParams& params);

// base + tier * budgetPerTier, capped at maxBudget. Saturating, never overflows.
std::uint32_t encounterBudgetForTier(std::uint32_t tier, const TierEncounterParams& params);

// `table` with the heavy roles' weights raised for the tier (capped at kMaxRoleWeight).
std::vector<RoleBudget> roleTableForTier(const std::vector<RoleBudget>& table, std::uint32_t tier, const TierEncounterParams& params);

// An encounter for the region under a world position: the tier lookup feeds the budget and the roster.
// Problems are appended to `problems` and yield an empty encounter; never throws.
Encounter composeEncounterAt(const ForgedRegionTiers& tiers, double worldX, double worldZ, std::uint64_t seed, const std::string& faction, const std::string& biome,
    const TierEncounterParams& params, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems);

}
