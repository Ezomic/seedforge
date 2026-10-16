#include "ichor/world/coop_scaling.h"

#include "ichor/core/rng.h"

#include <cmath>

namespace ichor {

std::vector<std::string> validateCoopCurve(const CoopCurve& curve)
{
    std::vector<std::string> problems;
    if (curve.decayPercent > 100) {
        problems.push_back("decayPercent must be at most 100");
    }
    if (curve.firstStepPercent > kMaxCoopPercent) {
        problems.push_back("firstStepPercent must be at most " + std::to_string(kMaxCoopPercent));
    }
    if (curve.maxExtraPercent > kMaxCoopPercent) {
        problems.push_back("maxExtraPercent must be at most " + std::to_string(kMaxCoopPercent));
    }
    if (curve.maxPlayers == 0 || curve.maxPlayers > kMaxCoopPlayers) {
        problems.push_back("maxPlayers must be in 1.." + std::to_string(kMaxCoopPlayers));
    }
    return problems;
}

std::uint32_t effectivePlayers(std::uint32_t players, const CoopCurve& curve)
{
    const std::uint32_t cap = curve.maxPlayers == 0 ? 1 : curve.maxPlayers;
    if (players == 0) {
        return 1;
    }
    return players > cap ? cap : players;
}

std::uint32_t effectivePlayers(double players, const CoopCurve& curve)
{
    if (std::isnan(players) || players < 1.0) {
        return 1;
    }
    const std::uint32_t cap = curve.maxPlayers == 0 ? 1 : curve.maxPlayers;
    if (players >= static_cast<double>(cap)) {
        return cap;
    }
    return static_cast<std::uint32_t>(players);
}

std::uint32_t coopExtraPercent(std::uint32_t players, const CoopCurve& curve)
{
    const std::uint32_t n = effectivePlayers(players, curve);
    std::uint64_t total = 0;
    std::uint64_t step = static_cast<std::uint64_t>(curve.firstStepPercent) * 1000;
    for (std::uint32_t k = 1; k < n; ++k) {
        total += step;
        step = step * curve.decayPercent / 100;
    }
    const std::uint64_t percent = total / 1000;
    return static_cast<std::uint32_t>(percent < curve.maxExtraPercent ? percent : curve.maxExtraPercent);
}

ScaledEncounter scaleEncounterForCoop(const EncounterRequest& request, std::uint32_t players, const CoopCurve& curve, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems)
{
    ScaledEncounter out;
    out.encounter = composeEncounter(request, table, skins, problems);
    out.baseMembers = static_cast<std::uint32_t>(out.encounter.members.size());
    const std::vector<std::string> curveProblems = validateCoopCurve(curve);
    problems.insert(problems.end(), curveProblems.begin(), curveProblems.end());
    if (!curveProblems.empty() || out.encounter.members.empty()) {
        return out;
    }

    out.players = effectivePlayers(players, curve);
    for (std::uint32_t k = 2; k <= out.players; ++k) {
        const std::uint64_t slicePercent = coopExtraPercent(k, curve) - coopExtraPercent(k - 1, curve);
        const std::uint64_t sliceBudget = static_cast<std::uint64_t>(request.budget) * slicePercent / 100;
        if (sliceBudget == 0) {
            continue;
        }
        EncounterRequest slice = request;
        slice.budget = static_cast<std::uint32_t>(sliceBudget > 0xFFFFFFFFu ? 0xFFFFFFFFu : sliceBudget);
        slice.seed = Rng(request.seed).derive("coop" + std::to_string(k)).nextU64();
        const Encounter extra = composeEncounter(slice, table, skins, problems);
        for (const EncounterMember& member : extra.members) {
            if (out.encounter.members.size() >= kMaxEncounterMembers) {
                break;
            }
            out.encounter.members.push_back(member);
            out.encounter.spent += member.cost;
            out.extraBudget += member.cost;
        }
    }
    return out;
}

}
