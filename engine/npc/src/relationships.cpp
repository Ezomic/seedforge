#include "ichor/npc/relationships.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <set>
#include <utility>

namespace ichor {

namespace {

constexpr std::uint32_t kLocalChanceOutOf = 4; // three in four partners share the settlement
constexpr std::size_t kKindCount = 5;

std::size_t findRoot(std::vector<std::size_t>& parent, std::size_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

}

bool isMutual(RelationKind kind)
{
    return kind == RelationKind::Family || kind == RelationKind::Rival || kind == RelationKind::Lover;
}

std::vector<RelationshipEdge> generateRelationships(std::uint64_t worldSeed, const std::vector<RelationshipNpc>& npcs)
{
    std::vector<RelationshipNpc> people;
    for (const RelationshipNpc& npc : npcs) {
        if (npc.tier == NpcTier::Regular) {
            people.push_back(npc);
        }
    }
    std::sort(people.begin(), people.end(), [](const RelationshipNpc& l, const RelationshipNpc& r) {
        return l.id != r.id ? l.id < r.id : l.settlement < r.settlement;
    });
    people.erase(std::unique(people.begin(), people.end(), [](const RelationshipNpc& l, const RelationshipNpc& r) { return l.id == r.id; }), people.end());

    const std::size_t count = people.size();
    std::vector<std::size_t> degree(count, 0);
    std::vector<std::size_t> kin(count);
    std::iota(kin.begin(), kin.end(), std::size_t{0});
    std::set<std::pair<std::size_t, std::size_t>> linked;
    std::vector<RelationshipEdge> edges;

    for (std::size_t i = 0; i < count; ++i) {
        Rng rng(hashCombine(worldSeed, hash64(people[i].id)));
        const std::size_t target = kMinRelationships + rng.range(0, static_cast<std::uint32_t>(kMaxRelationships - kMinRelationships));

        while (degree[i] < target) {
            std::vector<std::size_t> local;
            std::vector<std::size_t> any;
            for (std::size_t j = 0; j < count; ++j) {
                if (j == i || degree[j] >= kMaxRelationships || linked.count({std::min(i, j), std::max(i, j)}) != 0) {
                    continue;
                }
                any.push_back(j);
                if (people[j].settlement == people[i].settlement) {
                    local.push_back(j);
                }
            }
            if (any.empty()) {
                break;
            }
            const bool preferLocal = !local.empty() && rng.range(0, kLocalChanceOutOf - 1) != 0;
            const std::vector<std::size_t>& pool = preferLocal ? local : any;
            const std::size_t j = pool[rng.range(0, static_cast<std::uint32_t>(pool.size() - 1))];

            auto kind = static_cast<RelationKind>(rng.range(0, static_cast<std::uint32_t>(kKindCount - 1)));
            if (kind == RelationKind::Family && findRoot(kin, i) == findRoot(kin, j)) {
                kind = RelationKind::Rival;
            }
            if (kind == RelationKind::Family) {
                kin[findRoot(kin, i)] = findRoot(kin, j);
            }

            RelationshipEdge edge;
            const bool forward = isMutual(kind) ? people[i].id < people[j].id : rng.range(0, 1) == 0;
            edge.a = forward ? people[i].id : people[j].id;
            edge.b = forward ? people[j].id : people[i].id;
            edge.kind = kind;
            edge.strength = static_cast<int>(rng.range(kMinStrength, kMaxStrength));
            edges.push_back(std::move(edge));
            linked.insert({std::min(i, j), std::max(i, j)});
            ++degree[i];
            ++degree[j];
        }
    }

    std::sort(edges.begin(), edges.end(), [](const RelationshipEdge& l, const RelationshipEdge& r) {
        return l.a != r.a ? l.a < r.a : l.b < r.b;
    });
    return edges;
}

std::string canonicalForm(const std::vector<RelationshipEdge>& edges)
{
    std::string text;
    for (const RelationshipEdge& edge : edges) {
        if (!text.empty()) {
            text += ';';
        }
        text += edge.a;
        text += '|';
        text += edge.b;
        text += '|';
        text += relationKindName(edge.kind);
        text += '|';
        text += std::to_string(edge.strength);
    }
    return text;
}

std::uint64_t relationshipsDigest(const std::vector<RelationshipEdge>& edges)
{
    return hash64(canonicalForm(edges));
}

}
