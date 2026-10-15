#pragma once

#include "ichor/factions/factions.h"
#include "ichor/factions/standing.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

enum class StandingDisplayStatus : std::uint8_t {
    Ok,
    UnknownFaction,
    InvalidStanding,
    NoTiers,
};

struct StandingDisplayTier {
    std::string id;
    std::string name;
    std::int32_t threshold = 0;
};

// What one faction's standing looks like to the player: the tier it sits in, how
// far it is toward the next one, and exactly which gates each tier opens. Gates
// are named by id; turning an id into player-facing text is the UI's job.
struct StandingDisplay {
    StandingDisplayStatus status = StandingDisplayStatus::Ok;
    std::string faction;
    std::int32_t standing = 0;

    StandingDisplayTier tier;
    std::size_t tierIndex = 0;
    std::size_t tierCount = 0;

    // The next tier up. Empty, with `atTopTier` set, when this is the highest tier.
    bool atTopTier = false;
    StandingDisplayTier nextTier;
    std::int32_t pointsToNext = 0;
    // 0 at this tier's threshold, approaching 1 just below the next; 1 at the top tier.
    double progress = 0.0;

    // Gates this tier is the first to open, ascending by gate id.
    std::vector<std::string> unlocksAtTier;
    // Every gate open at this standing (this tier and all below), ascending by gate id.
    std::vector<std::string> unlockedNow;
    // Gates the next tier opens, so the player sees what the next step buys.
    std::vector<std::string> unlocksAtNextTier;
};

// Builds the display model for `faction` at `standing`. `tiers` and `gates` should be
// valid per parseStandingTiers and parseStandingGates, but a hostile set never
// crashes: gates for other factions or unknown tiers are ignored, duplicate gate
// ids are listed once, and an empty tier list yields NoTiers. A standing outside
// [kStandingMin, kStandingMax] is clamped; a NaN or infinite one yields
// InvalidStanding. A faction missing from `factions` yields UnknownFaction. On any
// non-Ok status only `status` and `faction` are set.
StandingDisplay buildStandingDisplay(std::string_view faction, double standing, const std::vector<Faction>& factions,
    const std::vector<StandingTier>& tiers, const std::vector<StandingGate>& gates);

}
