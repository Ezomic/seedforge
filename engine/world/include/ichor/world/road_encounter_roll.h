#pragma once

#include "ichor/world/encounter_budget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 11: bandits know the road bonus too. Whether a road segment is watched by a
// faction patrol decides how likely an ambient ambush is.
enum class RoadSafety { Patrolled, Unpatrolled };

constexpr double kMaxRoadSegmentMetres = 100000.0;

struct RoadEncounterParams {
    std::uint32_t unpatrolledPermillePerKm = 60;
    std::uint32_t patrolledPermillePerKm = 5;
    // Applied to the chance at night (1000 = no change).
    std::uint32_t nightMultiplierPermille = 2000;
    std::uint32_t maxChancePermille = 250;
    std::uint32_t baseBudget = 6;
    std::uint32_t budgetPerKm = 3;
    std::uint32_t nightBudgetBonus = 2;
    std::uint32_t maxBudget = 24;
    // Night is [nightStartHour, 24) plus [0, nightEndHour); hours in [0, 24).
    double nightStartHour = 20.0;
    double nightEndHour = 5.0;
};

struct RoadEncounterQuery {
    std::uint64_t seed = 0;
    std::uint64_t segmentId = 0;
    double lengthMetres = 0.0;
    RoadSafety safety = RoadSafety::Unpatrolled;
    double hourOfDay = 12.0;
    std::string faction;
    std::string biome;
};

struct RoadEncounterRoll {
    bool occurs = false;
    std::uint32_t chancePermille = 0;
    std::uint32_t budget = 0;
    Encounter encounter;
};

std::vector<std::string> validateRoadEncounterParams(const RoadEncounterParams& params);

// Decides deterministically whether an ambush happens on this segment and, if so,
// composes it with composeEncounter. Pure in its arguments. Non-finite, negative or
// out of range inputs, invalid tuning and a broken role table append to `problems`
// and yield no encounter; never throws.
RoadEncounterRoll rollRoadEncounter(const RoadEncounterQuery& query, const RoadEncounterParams& params, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems);

}
