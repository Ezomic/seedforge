#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstdint>
#include <string>

namespace ichor {

// The encounter budget for a region, read from the ledger instead of the player's
// level (doc 04, pacing). The result is the `budget` of an EncounterRequest.
//
// Three signals, all integers and all pure:
//  - kind "escalation" and kind "death" facts about the region, each fresh fact
//    adding its weight and older facts adding less (the queryTension decay);
//  - the latest kind "faction.strength" fact about the faction, a decimal 0 to 1000
//    in its value, adding up to maxStrengthBonus.
// Adding escalation or death facts never lowers the budget.
struct LedgerBudgetParams {
    std::string region;
    std::string faction;
    std::uint64_t now = 0;
    std::uint64_t horizonTicks = 360000;
    std::uint32_t baseBudget = 8;
    std::uint32_t minBudget = 1;
    std::uint32_t maxBudget = 200;
    std::uint32_t maxStrengthBonus = 16;
    std::uint32_t escalationWeight = 2;
    std::uint32_t deathWeight = 1;
};

constexpr std::uint32_t kMaxLedgerBudget = 10000;
constexpr std::uint32_t kMaxFactionStrength = 1000;

// Clamped to 1..kMaxLedgerBudget and to the params' own min and max (a min above the
// max is lowered to the max). Never throws.
std::uint32_t deriveLedgerBudget(const ConsequenceLedger& ledger, const LedgerBudgetParams& params);

}
