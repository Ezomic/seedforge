#include "ichor/factions/hostility.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

namespace ichor {

namespace {

std::vector<std::uint32_t> sortedUnique(const std::vector<std::uint32_t>& ids)
{
    std::vector<std::uint32_t> out = ids;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::uint64_t intersectionSize(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    std::uint64_t n = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j])
            ++i;
        else if (b[j] < a[i])
            ++j;
        else {
            ++n;
            ++i;
            ++j;
        }
    }
    return n;
}

bool stanceValid(const FactionStance& s)
{
    return static_cast<unsigned>(s.order) <= 2u && static_cast<unsigned>(s.law) <= 2u;
}

std::int32_t axisSteps(int a, int b)
{
    return std::abs(a - b);
}

Relation classify(std::int32_t hostility)
{
    if (hostility <= kAllyAtMost)
        return Relation::Allied;
    if (hostility >= kHostileAtLeast)
        return Relation::Hostile;
    return Relation::Neutral;
}

}

std::int32_t ideologicalDistance(const FactionStance& a, const FactionStance& b)
{
    return (axisSteps(static_cast<int>(a.order), static_cast<int>(b.order)) + axisSteps(static_cast<int>(a.law), static_cast<int>(b.law))) * kStepDistance;
}

std::int32_t resourceOverlap(const FactionClaim& a, const FactionClaim& b)
{
    const auto aCells = sortedUnique(a.cells);
    const auto bCells = sortedUnique(b.cells);
    const auto aRes = sortedUnique(a.resources);
    const auto bRes = sortedUnique(b.resources);
    const std::uint64_t shared = intersectionSize(aCells, bCells) + intersectionSize(aRes, bRes);
    const std::uint64_t smaller = std::min<std::uint64_t>(aCells.size() + aRes.size(), bCells.size() + bRes.size());
    if (smaller == 0)
        return 0;
    return static_cast<std::int32_t>(shared * kMaxOverlap / smaller);
}

bool computeHostilityMatrix(const std::vector<FactionStance>& stances, const std::vector<FactionClaim>& claims, std::vector<HostilityEntry>& out,
    std::vector<std::string>& problems)
{
    out.clear();
    const std::size_t problemsBefore = problems.size();

    std::vector<const FactionStance*> sorted;
    sorted.reserve(stances.size());
    for (std::size_t i = 0; i < stances.size(); ++i) {
        if (!stanceValid(stances[i]))
            problems.push_back("stances[" + std::to_string(i) + "]: order or law out of range");
        sorted.push_back(&stances[i]);
    }
    std::sort(sorted.begin(), sorted.end(), [](const FactionStance* x, const FactionStance* y) { return x->faction < y->faction; });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i]->faction == sorted[i - 1]->faction)
            problems.push_back("stances: duplicate faction '" + sorted[i]->faction + "'");

    std::vector<const FactionClaim*> claimed;
    claimed.reserve(claims.size());
    for (const auto& c : claims)
        claimed.push_back(&c);
    std::sort(claimed.begin(), claimed.end(), [](const FactionClaim* x, const FactionClaim* y) { return x->faction < y->faction; });
    for (std::size_t i = 0; i < claimed.size(); ++i) {
        if (i > 0 && claimed[i]->faction == claimed[i - 1]->faction)
            problems.push_back("claims: duplicate faction '" + claimed[i]->faction + "'");
        const bool known = std::any_of(sorted.begin(), sorted.end(), [&](const FactionStance* st) { return st->faction == claimed[i]->faction; });
        if (!known)
            problems.push_back("claims: unknown faction '" + claimed[i]->faction + "'");
    }
    if (problems.size() != problemsBefore)
        return false;

    const FactionClaim empty;
    auto claimOf = [&](const std::string& id) -> const FactionClaim& {
        auto it = std::lower_bound(claimed.begin(), claimed.end(), id, [](const FactionClaim* c, const std::string& key) { return c->faction < key; });
        return it != claimed.end() && (*it)->faction == id ? **it : empty;
    };

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        for (std::size_t j = i + 1; j < sorted.size(); ++j) {
            HostilityEntry e;
            e.a = sorted[i]->faction;
            e.b = sorted[j]->faction;
            e.ideology = ideologicalDistance(*sorted[i], *sorted[j]);
            e.overlap = resourceOverlap(claimOf(e.a), claimOf(e.b));
            e.hostility = e.ideology + e.overlap;
            e.relation = classify(e.hostility);
            const bool opposed = axisSteps(static_cast<int>(sorted[i]->order), static_cast<int>(sorted[j]->order)) == 2
                || axisSteps(static_cast<int>(sorted[i]->law), static_cast<int>(sorted[j]->law)) == 2;
            e.oddBedfellows = opposed && e.relation != Relation::Hostile;
            out.push_back(std::move(e));
        }
    }
    return true;
}

const HostilityEntry* findHostility(const std::vector<HostilityEntry>& matrix, std::string_view a, std::string_view b)
{
    if (a > b)
        std::swap(a, b);
    for (const auto& e : matrix)
        if (e.a == a && e.b == b)
            return &e;
    return nullptr;
}

std::uint64_t hostilityDigest(const std::vector<HostilityEntry>& matrix)
{
    std::uint64_t h = hash64("hostility");
    for (const auto& e : matrix) {
        h = hashCombine(h, hash64(e.a));
        h = hashCombine(h, hash64(e.b));
        h = hashCombine(h, static_cast<std::uint64_t>(e.ideology));
        h = hashCombine(h, static_cast<std::uint64_t>(e.overlap));
    }
    return h;
}

}
