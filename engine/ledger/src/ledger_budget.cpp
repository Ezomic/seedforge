#include "ichor/ledger/ledger_budget.h"

#include "ichor/ledger/ledger_query.h"

#include <algorithm>
#include <string_view>

namespace ichor {

namespace {

constexpr std::uint64_t kScoreScale = 1000;

std::uint64_t pressure(const ConsequenceLedger& ledger, const LedgerBudgetParams& params, std::string_view kind, std::uint32_t weight)
{
    if (params.region.empty() || weight == 0) {
        return 0;
    }
    TensionQuery query;
    query.kinds.push_back({std::string(kind), weight});
    query.subject = params.region;
    query.now = params.now;
    query.horizonTicks = params.horizonTicks;
    query.limit = kMaxQueryLimit;
    std::uint64_t total = 0;
    for (const ScoredFact& scored : queryTension(ledger, query)) {
        total += scored.score;
    }
    return total / kScoreScale;
}

std::uint32_t parseStrength(std::string_view text)
{
    if (text.empty() || text.size() > 4) {
        return 0;
    }
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return value > kMaxFactionStrength ? 0 : value;
}

std::uint32_t factionStrength(const ConsequenceLedger& ledger, std::string_view faction)
{
    if (faction.empty()) {
        return 0;
    }
    const Fact* latest = nullptr;
    for (const Fact* fact : ledger.bySubjectAndKind(faction, "faction.strength")) {
        if (latest == nullptr || fact->tick >= latest->tick) {
            latest = fact;
        }
    }
    return latest == nullptr ? 0 : parseStrength(latest->value);
}

}

std::uint32_t deriveLedgerBudget(const ConsequenceLedger& ledger, const LedgerBudgetParams& params)
{
    const std::uint64_t base = std::min(params.baseBudget, kMaxLedgerBudget);
    const std::uint64_t bonus = std::min(params.maxStrengthBonus, kMaxLedgerBudget);
    const std::uint64_t strength = bonus * factionStrength(ledger, params.faction) / kMaxFactionStrength;
    const std::uint64_t raised = pressure(ledger, params, "escalation", params.escalationWeight) + pressure(ledger, params, "death", params.deathWeight);

    const std::uint64_t ceiling = std::clamp<std::uint64_t>(params.maxBudget, 1, kMaxLedgerBudget);
    const std::uint64_t floor = std::clamp<std::uint64_t>(params.minBudget, 1, ceiling);
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(base + strength + raised, floor, ceiling));
}

}
