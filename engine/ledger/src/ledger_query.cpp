#include "ichor/ledger/ledger_query.h"

#include <algorithm>
#include <map>
#include <string_view>

namespace ichor {

std::uint32_t factScore(std::uint32_t weight, std::uint64_t tick, std::uint64_t now, std::uint64_t horizonTicks)
{
    const std::uint64_t horizon = std::clamp<std::uint64_t>(horizonTicks, 1, kMaxHorizonTicks);
    const std::uint64_t age = std::min(now > tick ? now - tick : 0, kMaxFactAgeTicks);
    const std::uint64_t capped = std::min(weight, kMaxKindWeight);
    return static_cast<std::uint32_t>(capped * 1000 * horizon / (horizon + age));
}

std::vector<ScoredFact> queryTension(const ConsequenceLedger& ledger, const TensionQuery& query)
{
    const std::size_t limit = std::min(query.limit, kMaxQueryLimit);
    if (limit == 0) {
        return {};
    }
    std::map<std::string_view, std::uint32_t> weights;
    for (const KindWeight& entry : query.kinds) {
        std::uint32_t& slot = weights[entry.kind];
        slot = std::max(slot, std::min(entry.weight, kMaxKindWeight));
    }
    const auto stronger = [](const ScoredFact& a, const ScoredFact& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.tick != b.tick) {
            return a.tick > b.tick;
        }
        return a.index < b.index;
    };
    std::vector<ScoredFact> kept;
    const std::vector<Fact>& facts = ledger.facts();
    for (std::size_t i = 0; i < facts.size(); ++i) {
        const Fact& fact = facts[i];
        if (!query.subject.empty() && fact.subject != query.subject) {
            continue;
        }
        const auto weight = weights.find(std::string_view{fact.kind});
        if (weight == weights.end()) {
            continue;
        }
        const std::uint32_t score = factScore(weight->second, fact.tick, query.now, query.horizonTicks);
        if (score == 0) {
            continue;
        }
        kept.push_back({i, fact.tick, score});
        if (kept.size() >= limit * 4) {
            std::partial_sort(kept.begin(), kept.begin() + limit, kept.end(), stronger);
            kept.resize(limit);
        }
    }
    const std::size_t take = std::min(kept.size(), limit);
    std::partial_sort(kept.begin(), kept.begin() + take, kept.end(), stronger);
    kept.resize(take);
    return kept;
}

}
