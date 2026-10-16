#include "ichor/npc/relationships.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

std::vector<RelationshipNpc> town(int count, int settlements = 3)
{
    std::vector<RelationshipNpc> npcs;
    for (int i = 0; i < count; ++i) {
        npcs.push_back({"npc_" + std::to_string(1000 + i), NpcTier::Regular, "town_" + std::to_string(i % settlements)});
    }
    return npcs;
}

std::map<std::string, int> degrees(const std::vector<RelationshipEdge>& edges)
{
    std::map<std::string, int> out;
    for (const RelationshipEdge& e : edges) {
        ++out[e.a];
        ++out[e.b];
    }
    return out;
}

void requireSound(const std::vector<RelationshipEdge>& edges, const std::vector<RelationshipNpc>& npcs)
{
    std::set<std::string> ids;
    for (const RelationshipNpc& n : npcs) {
        if (n.tier == NpcTier::Regular) {
            ids.insert(n.id);
        }
    }
    std::set<std::pair<std::string, std::string>> pairs;
    for (const RelationshipEdge& e : edges) {
        REQUIRE(e.a != e.b);
        REQUIRE(ids.count(e.a) == 1);
        REQUIRE(ids.count(e.b) == 1);
        REQUIRE(e.strength >= kMinStrength);
        REQUIRE(e.strength <= kMaxStrength);
        if (isMutual(e.kind)) {
            REQUIRE(e.a < e.b);
        }
        REQUIRE(pairs.insert({std::min(e.a, e.b), std::max(e.a, e.b)}).second);
    }
    REQUIRE(std::is_sorted(edges.begin(), edges.end(), [](const RelationshipEdge& l, const RelationshipEdge& r) {
        return l.a != r.a ? l.a < r.a : l.b < r.b;
    }));
}

bool familyIsForest(const std::vector<RelationshipEdge>& edges)
{
    std::map<std::string, std::string> parent;
    auto root = [&](std::string x) {
        while (parent.count(x) != 0 && parent[x] != x) {
            x = parent[x];
        }
        return x;
    };
    for (const RelationshipEdge& e : edges) {
        if (e.kind != RelationKind::Family) {
            continue;
        }
        const std::string ra = root(e.a);
        const std::string rb = root(e.b);
        if (ra == rb) {
            return false;
        }
        parent[ra] = rb;
        parent[rb] = rb;
    }
    return true;
}

}

TEST_CASE("every Regular has three to five relationships", "[npc][relationships]")
{
    for (std::uint64_t seed : {1ull, 42ull, 0xdeadbeefull}) {
        const auto npcs = town(80);
        const auto edges = generateRelationships(seed, npcs);
        requireSound(edges, npcs);
        const auto deg = degrees(edges);
        for (const RelationshipNpc& n : npcs) {
            REQUIRE(deg.count(n.id) == 1);
            REQUIRE(deg.at(n.id) >= static_cast<int>(kMinRelationships));
            REQUIRE(deg.at(n.id) <= static_cast<int>(kMaxRelationships));
        }
    }
}

TEST_CASE("every kind appears in a typical web", "[npc][relationships]")
{
    std::set<RelationKind> seen;
    for (const RelationshipEdge& e : generateRelationships(7, town(80))) {
        seen.insert(e.kind);
    }
    REQUIRE(seen.size() == 5);
}

TEST_CASE("most relationships stay inside a settlement", "[npc][relationships]")
{
    const auto npcs = town(90);
    std::map<std::string, std::string> home;
    for (const RelationshipNpc& n : npcs) {
        home[n.id] = n.settlement;
    }
    const auto edges = generateRelationships(3, npcs);
    const auto local = std::count_if(edges.begin(), edges.end(), [&](const RelationshipEdge& e) { return home[e.a] == home[e.b]; });
    REQUIRE(local * 2 > static_cast<long>(edges.size()));
}

TEST_CASE("kinship has no cycles", "[npc][relationships]")
{
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        REQUIRE(familyIsForest(generateRelationships(seed, town(60, 1))));
    }
}

TEST_CASE("the web is deterministic and independent of input order", "[npc][relationships]")
{
    auto npcs = town(70);
    const auto expected = generateRelationships(99, npcs);
    REQUIRE(canonicalForm(expected) == canonicalForm(generateRelationships(99, npcs)));
    std::reverse(npcs.begin(), npcs.end());
    REQUIRE(canonicalForm(expected) == canonicalForm(generateRelationships(99, npcs)));
    std::rotate(npcs.begin(), npcs.begin() + 13, npcs.end());
    REQUIRE(relationshipsDigest(expected) == relationshipsDigest(generateRelationships(99, npcs)));
}

TEST_CASE("the seed changes the web", "[npc][relationships]")
{
    const auto npcs = town(70);
    REQUIRE(relationshipsDigest(generateRelationships(1, npcs)) != relationshipsDigest(generateRelationships(2, npcs)));
}

TEST_CASE("the web is pinned to a golden digest", "[npc][relationships]")
{
    const auto edges = generateRelationships(1234, town(12, 2));
    REQUIRE_FALSE(edges.empty());
    INFO(canonicalForm(edges));
    REQUIRE(relationshipsDigest(edges) == 0xdc55ec278f506fffull);
}

TEST_CASE("no edges for zero or one NPC", "[npc][relationships]")
{
    REQUIRE(generateRelationships(1, {}).empty());
    REQUIRE(generateRelationships(1, town(1)).empty());
}

TEST_CASE("two NPCs share one edge", "[npc][relationships]")
{
    const auto npcs = town(2);
    const auto edges = generateRelationships(5, npcs);
    REQUIRE(edges.size() == 1);
    requireSound(edges, npcs);
}

TEST_CASE("small groups have no isolated NPC", "[npc][relationships]")
{
    for (int n = 2; n <= 12; ++n) {
        const auto npcs = town(n, 2);
        const auto edges = generateRelationships(8, npcs);
        requireSound(edges, npcs);
        const auto deg = degrees(edges);
        for (const RelationshipNpc& p : npcs) {
            REQUIRE(deg.count(p.id) == 1);
            REQUIRE(deg.at(p.id) <= static_cast<int>(kMaxRelationships));
            if (n >= 4) {
                REQUIRE(deg.at(p.id) >= static_cast<int>(kMinRelationships));
            }
        }
    }
}

TEST_CASE("the minimum holds across many seeds and sizes", "[npc][relationships]")
{
    for (int n = 4; n <= 30; ++n) {
        for (std::uint64_t seed = 0; seed < 60; ++seed) {
            const auto npcs = town(n, 2);
            const auto deg = degrees(generateRelationships(seed, npcs));
            for (const RelationshipNpc& p : npcs) {
                REQUIRE(deg.count(p.id) == 1);
                REQUIRE(deg.at(p.id) >= static_cast<int>(kMinRelationships));
                REQUIRE(deg.at(p.id) <= static_cast<int>(kMaxRelationships));
            }
        }
    }
}

TEST_CASE("different seeds give different webs", "[npc][relationships]")
{
    std::set<std::uint64_t> digests;
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        digests.insert(relationshipsDigest(generateRelationships(seed, town(40))));
    }
    REQUIRE(digests.size() == 8);
}

TEST_CASE("a thousand NPCs stay bounded and fast", "[npc][relationships]")
{
    const auto npcs = town(1000, 10);
    const auto start = std::chrono::steady_clock::now();
    const auto edges = generateRelationships(77, npcs);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // Generous because tests run in parallel under ASan; this only catches a blow-up, the degree checks below catch wrong output.
    REQUIRE(elapsed < std::chrono::seconds(30));
    requireSound(edges, npcs);
    const auto deg = degrees(edges);
    REQUIRE(deg.size() == 1000);
    for (const auto& [id, d] : deg) {
        REQUIRE(d >= 3);
        REQUIRE(d <= 5);
    }
    REQUIRE(familyIsForest(edges));
}

TEST_CASE("duplicate ids collapse to one NPC", "[npc][relationships]")
{
    const auto npcs = town(10);
    auto doubled = npcs;
    doubled.push_back({npcs[3].id, NpcTier::Regular, "elsewhere"});
    doubled.push_back(npcs[5]);
    auto flipped = doubled;
    std::reverse(flipped.begin(), flipped.end());
    const auto edges = generateRelationships(4, doubled);
    REQUIRE(canonicalForm(edges) == canonicalForm(generateRelationships(4, flipped)));
    requireSound(edges, npcs);
    REQUIRE(degrees(edges).size() == 10);
}

TEST_CASE("only Regulars take part", "[npc][relationships]")
{
    auto npcs = town(10);
    npcs.push_back({"principal_1", NpcTier::Principal, "town_0"});
    npcs.push_back({"resident_1", NpcTier::Resident, "town_0"});
    npcs.push_back({"crowd_1", NpcTier::Crowd, "town_0"});
    REQUIRE(canonicalForm(generateRelationships(4, npcs)) == canonicalForm(generateRelationships(4, town(10))));
}

TEST_CASE("extreme seeds work", "[npc][relationships]")
{
    for (std::uint64_t seed : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
        const auto npcs = town(40);
        const auto edges = generateRelationships(seed, npcs);
        requireSound(edges, npcs);
        REQUIRE(degrees(edges).size() == 40);
    }
}

TEST_CASE("canonical form lists edges by id", "[npc][relationships]")
{
    const std::vector<RelationshipEdge> edges = {{"a", "b", RelationKind::Lover, 3}, {"b", "c", RelationKind::Creditor, 1}};
    REQUIRE(canonicalForm(edges) == "a|b|lover|3;b|c|creditor|1");
    REQUIRE(canonicalForm({}).empty());
}
