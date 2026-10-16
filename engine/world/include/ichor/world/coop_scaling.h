#pragma once

#include "ichor/world/encounter_budget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Co-op difficulty (doc 15): add enemies, never inflate health. Scaling only ever
// appends members to an encounter; an enemy's role, skin and cost are never touched,
// so no health or damage figure can change with the party size.
//
// Each extra player adds a step of extra budget. The first step is firstStepPercent
// of the base budget and every later step is decayPercent of the one before, so the
// curve flattens. The sum is clamped to maxExtraPercent.
struct CoopCurve {
    std::uint32_t firstStepPercent = 60;
    std::uint32_t decayPercent = 75;
    std::uint32_t maxExtraPercent = 200;
    std::uint32_t maxPlayers = 8;
};

constexpr std::uint32_t kMaxCoopPlayers = 64;
constexpr std::uint32_t kMaxCoopPercent = 1000;

struct ScaledEncounter {
    Encounter encounter;
    std::uint32_t players = 1;
    std::uint32_t baseMembers = 0;
    std::uint32_t extraBudget = 0;
};

// Problems with a curve: out-of-range percents, maxPlayers outside 1..kMaxCoopPlayers.
std::vector<std::string> validateCoopCurve(const CoopCurve& curve);

// Maps a raw player count to the count that is used: 0 becomes 1, anything above
// curve.maxPlayers is clamped.
std::uint32_t effectivePlayers(std::uint32_t players, const CoopCurve& curve);

// Same for an untrusted real value: NaN and anything below 1 (including -infinity) become 1; +infinity clamps to the maximum.
std::uint32_t effectivePlayers(double players, const CoopCurve& curve);

// Total extra budget, in percent of the base budget, for a party of this size.
// Zero for one player, non-decreasing, never above curve.maxExtraPercent.
std::uint32_t coopExtraPercent(std::uint32_t players, const CoopCurve& curve);

// Composes the single-player encounter from the request, then appends one slice of
// extra members per extra player, each composed from a seed derived from the request
// seed and the player index. The members for N players are therefore always a prefix
// of the members for N+1, and the single-player members are always unchanged. Total
// members stay within kMaxEncounterMembers. Per-role maxCount applies within each
// slice, not across the whole encounter. Deterministic. An invalid curve is reported
// in `problems` and the unscaled encounter is returned.
ScaledEncounter scaleEncounterForCoop(const EncounterRequest& request, std::uint32_t players, const CoopCurve& curve, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems);

}
