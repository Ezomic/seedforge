#include "ichor/ledger/tension_sampling.h"

#include "ichor/core/rng.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace ichor {

namespace {

bool validId(const std::string& id)
{
    return !id.empty() && id.size() <= kMaxTensionIdBytes;
}

// Thousandths, so the score math is integer. No libm: only a comparison and a multiply.
std::uint64_t weightMilli(double weight)
{
    if (!(weight > 0.0)) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::min(weight, kMaxSourceWeight) * 1000.0);
}

std::uint32_t weighted(std::uint64_t raw, std::uint64_t milli)
{
    const std::uint64_t capped = std::min<std::uint64_t>(raw, std::uint64_t{1} << 32);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(capped * milli / 1000, kMaxTensionScore));
}

bool before(const Tension& a, const Tension& b)
{
    if (a.score != b.score) {
        return a.score > b.score;
    }
    return std::tie(a.kind, a.wanter, a.holder, a.stake) < std::tie(b.kind, b.wanter, b.holder, b.stake);
}

void add(std::vector<Tension>& out, Tension t)
{
    if (t.score > 0) {
        out.push_back(std::move(t));
    }
}

void collectLedger(const TensionInputs& in, std::uint64_t milli, std::vector<Tension>& out)
{
    if (in.ledger == nullptr || milli == 0) {
        return;
    }
    TensionQuery query = in.ledgerQuery;
    query.limit = kMaxQueryLimit;
    const std::vector<Fact>& facts = in.ledger->facts();
    for (const ScoredFact& scored : queryTension(*in.ledger, query)) {
        const Fact& fact = facts[scored.index];
        if (!validId(fact.subject)) {
            continue;
        }
        add(out, {TensionKind::Consequence, fact.subject, std::string{kPlayerHolder}, "fact:" + fact.id, weighted(scored.score, milli)});
    }
}

void collectHostility(const TensionInputs& in, const TensionConfig& config, std::uint64_t milli, std::vector<Tension>& out)
{
    if (in.hostility == nullptr || milli == 0) {
        return;
    }
    std::map<std::pair<std::string, std::string>, std::int32_t> best;
    for (const HostilityEntry& e : *in.hostility) {
        if (!validId(e.a) || !validId(e.b) || e.a == e.b || e.hostility < std::max(config.hostilityThreshold, 1)) {
            continue;
        }
        const auto key = e.a < e.b ? std::make_pair(e.a, e.b) : std::make_pair(e.b, e.a);
        std::int32_t& slot = best[key];
        slot = std::max(slot, std::min(e.hostility, kMaxHostility));
    }
    for (const auto& [pair, hostility] : best) {
        add(out, {TensionKind::Hostility, pair.first, pair.second, "hostile", weighted(static_cast<std::uint64_t>(hostility) * 1000, milli)});
    }
}

void collectContested(const TensionInputs& in, std::uint64_t milli, std::vector<Tension>& out)
{
    const TerritoryResult* t = in.territory;
    if (t == nullptr || milli == 0 || t->ids.size() != t->strength.size()) {
        return;
    }
    const std::size_t factions = std::min(t->ids.size(), kMaxTerritoryFactions);
    if (factions < 2) {
        return;
    }
    std::map<std::pair<std::string, std::string>, Tension> best;
    for (std::size_t i = 0; i < factions; ++i) {
        const std::string& idI = t->ids[i];
        if (!validId(idI)) {
            continue;
        }
        for (std::size_t j = i + 1; j < factions; ++j) {
            const std::string& idJ = t->ids[j];
            if (!validId(idJ) || idI == idJ) {
                continue;
            }
            const std::size_t cells = std::min({t->strength[i].size(), t->strength[j].size(), kMaxTerritoryCells});
            std::uint64_t overlap = 0;
            std::uint64_t strengthI = 0;
            std::uint64_t strengthJ = 0;
            std::size_t firstCell = 0;
            bool seen = false;
            for (std::size_t c = 0; c < cells; ++c) {
                const std::uint32_t si = std::min<std::uint32_t>(t->strength[i][c], kMaxClaimStrength);
                const std::uint32_t sj = std::min<std::uint32_t>(t->strength[j][c], kMaxClaimStrength);
                if (si == 0 || sj == 0) {
                    continue;
                }
                if (!seen) {
                    seen = true;
                    firstCell = c;
                }
                overlap += std::min(si, sj);
                strengthI += si;
                strengthJ += sj;
            }
            if (!seen) {
                continue;
            }
            const bool iWants = strengthI != strengthJ ? strengthI > strengthJ : idI < idJ;
            Tension tension{TensionKind::Contested, iWants ? idI : idJ, iWants ? idJ : idI, "cell:" + std::to_string(firstCell), weighted(overlap, milli)};
            auto [slot, inserted] = best.try_emplace(idI < idJ ? std::make_pair(idI, idJ) : std::make_pair(idJ, idI), tension);
            if (!inserted && before(tension, slot->second)) {
                slot->second = std::move(tension);
            }
        }
    }
    for (auto& entry : best) {
        add(out, std::move(entry.second));
    }
}

}

std::vector<Tension> collectTensions(const TensionInputs& inputs, const TensionConfig& config)
{
    std::vector<Tension> out;
    collectLedger(inputs, weightMilli(config.consequenceWeight), out);
    collectHostility(inputs, config, weightMilli(config.hostilityWeight), out);
    collectContested(inputs, weightMilli(config.contestedWeight), out);
    std::sort(out.begin(), out.end(), before);
    if (out.size() > kMaxTensionCandidates) {
        out.resize(kMaxTensionCandidates);
    }
    return out;
}

std::vector<Tension> sampleTensions(const TensionInputs& inputs, const TensionConfig& config)
{
    std::vector<Tension> pool = collectTensions(inputs, config);
    const std::size_t cap = std::min(config.cap, kMaxTensions);
    if (cap == 0 || pool.empty()) {
        return {};
    }
    if (pool.size() <= cap) {
        return pool;
    }
    std::uint64_t total = 0;
    for (const Tension& t : pool) {
        total += t.score;
    }
    Rng rng(config.seed);
    std::vector<Tension> picked;
    while (picked.size() < cap && !pool.empty()) {
        std::uint64_t r = rng.nextU64() % total;
        std::size_t k = 0;
        while (k + 1 < pool.size() && r >= pool[k].score) {
            r -= pool[k].score;
            ++k;
        }
        total -= pool[k].score;
        picked.push_back(std::move(pool[k]));
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(k));
    }
    std::sort(picked.begin(), picked.end(), before);
    return picked;
}

}
