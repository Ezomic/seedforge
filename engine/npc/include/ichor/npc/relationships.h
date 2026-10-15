#pragma once

#include "ichor/data/npc_tiers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

struct RelationshipNpc {
    std::string id;
    NpcTier tier = NpcTier::Regular;
    std::string settlement;
};

// Family, rival and lover are mutual and stored once with a < b. Creditor
// (a is owed by b) and grudge (a holds it against b) point from a to b.
struct RelationshipEdge {
    std::string a;
    std::string b;
    RelationKind kind = RelationKind::Family;
    int strength = 1; // [kMinStrength, kMaxStrength]
};

constexpr int kMinStrength = 1;
constexpr int kMaxStrength = 5;

bool isMutual(RelationKind kind);

// Only Regulars take part (doc 10: three to five relationships each). Input
// order does not matter and duplicate ids collapse to one NPC (the one with
// the smallest settlement name). At most one
// edge per pair, no self edges, family edges form a forest, every Regular has
// kMinRelationships to kMaxRelationships edges (fewer only when there are too
// few other Regulars to reach that). Sorted by (a, b).
std::vector<RelationshipEdge> generateRelationships(std::uint64_t worldSeed, const std::vector<RelationshipNpc>& npcs);

std::string canonicalForm(const std::vector<RelationshipEdge>& edges);
std::uint64_t relationshipsDigest(const std::vector<RelationshipEdge>& edges);

}
