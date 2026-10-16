#include "ichor/world/road_encounter_roll.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

bool validHour(double hour)
{
    return std::isfinite(hour) && hour >= 0.0 && hour < 24.0;
}

bool isNight(double hour, const RoadEncounterParams& params)
{
    return hour >= params.nightStartHour || hour < params.nightEndHour;
}

}

std::vector<std::string> validateRoadEncounterParams(const RoadEncounterParams& params)
{
    std::vector<std::string> problems;
    if (params.maxChancePermille > 1000) {
        problems.push_back("maxChancePermille must be at most 1000");
    }
    if (params.maxBudget == 0) {
        problems.push_back("maxBudget must be positive");
    }
    if (params.nightMultiplierPermille > 100000) {
        problems.push_back("nightMultiplierPermille must be at most 100000");
    }
    if (params.unpatrolledPermillePerKm > 1000000 || params.patrolledPermillePerKm > 1000000 || params.budgetPerKm > 1000000) {
        problems.push_back("per-km tuning values must be at most 1000000");
    }
    if (!validHour(params.nightStartHour) || !validHour(params.nightEndHour)) {
        problems.push_back("night hours must be in [0, 24)");
    }
    return problems;
}

RoadEncounterRoll rollRoadEncounter(const RoadEncounterQuery& query, const RoadEncounterParams& params, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems)
{
    RoadEncounterRoll result;
    const std::vector<std::string> paramProblems = validateRoadEncounterParams(params);
    problems.insert(problems.end(), paramProblems.begin(), paramProblems.end());
    bool ok = paramProblems.empty();
    if (!std::isfinite(query.lengthMetres) || query.lengthMetres < 0.0 || query.lengthMetres > kMaxRoadSegmentMetres) {
        problems.push_back("road segment length must be finite and in 0.." + std::to_string(static_cast<long long>(kMaxRoadSegmentMetres)));
        ok = false;
    }
    if (!validHour(query.hourOfDay)) {
        problems.push_back("hour of day must be finite and in [0, 24)");
        ok = false;
    }
    if (query.safety != RoadSafety::Patrolled && query.safety != RoadSafety::Unpatrolled) {
        problems.push_back("unknown road safety");
        ok = false;
    }
    if (!ok) {
        return result;
    }

    const auto metres = static_cast<std::uint64_t>(query.lengthMetres);
    const bool night = isNight(query.hourOfDay, params);
    const std::uint64_t perKm = query.safety == RoadSafety::Patrolled ? params.patrolledPermillePerKm : params.unpatrolledPermillePerKm;
    std::uint64_t chance = perKm * metres / 1000;
    if (night) {
        chance = chance * params.nightMultiplierPermille / 1000;
    }
    result.chancePermille = static_cast<std::uint32_t>(std::min<std::uint64_t>(chance, params.maxChancePermille));

    std::uint64_t budget = params.baseBudget + static_cast<std::uint64_t>(params.budgetPerKm) * metres / 1000;
    if (night) {
        budget += params.nightBudgetBonus;
    }
    result.budget = static_cast<std::uint32_t>(std::min<std::uint64_t>(budget, params.maxBudget));

    if (result.chancePermille == 0) {
        return result;
    }
    Rng rng = Rng(hashCombine(query.seed, query.segmentId)).derive("road_encounter");
    if (rng.nextU32() % 1000 >= result.chancePermille) {
        return result;
    }

    EncounterRequest request;
    request.seed = rng.nextU64();
    request.budget = result.budget;
    request.faction = query.faction;
    request.biome = query.biome;
    result.encounter = composeEncounter(request, table, skins, problems);
    result.occurs = !result.encounter.members.empty();
    return result;
}

}
