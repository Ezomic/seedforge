#include "ichor/graph/minor_site.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <utility>

using namespace ichor::graph;
using namespace ichor::graph::site;

TEST_CASE("every seed and archetype yields a valid site in the time window", "[site]")
{
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        for (std::uint32_t a = 0; a <= kArchetypeCount; ++a) {
            Params p;
            p.seed = seed;
            p.archetype = a;
            const Result r = generate(p);
            REQUIRE(validateSite(r.graph).empty());
            REQUIRE(r.seconds == clearSeconds(r.graph));
            REQUIRE(r.seconds >= kMinSeconds);
            REQUIRE(r.seconds <= kMaxSeconds);
        }
    }
}

TEST_CASE("generation is deterministic", "[site]")
{
    Params p;
    p.seed = 12345;
    const Result a = generate(p);
    const Result b = generate(p);
    REQUIRE(a.graph.digest() == b.graph.digest());
    REQUIRE(a.archetype == b.archetype);
}

TEST_CASE("seeds give over a hundred distinct sites and every archetype", "[site]")
{
    std::set<std::pair<std::uint64_t, std::string>> sites;
    std::set<std::string> archetypes;
    for (std::uint64_t seed = 0; seed < 400; ++seed) {
        Params p;
        p.seed = seed;
        const Result r = generate(p);
        sites.insert({ r.graph.digest(), r.archetype });
        archetypes.insert(r.archetype);
    }
    REQUIRE(sites.size() > 100);
    REQUIRE(archetypes.size() == kArchetypeCount);
}

TEST_CASE("an explicit archetype is honoured", "[site]")
{
    for (std::uint32_t a = 0; a < kArchetypeCount; ++a) {
        Params p;
        p.seed = 7;
        p.archetype = a;
        REQUIRE(generate(p).archetype == archetypeName(a));
    }
    REQUIRE(archetypeName(kArchetypeCount).empty());
}

TEST_CASE("validateSite rejects broken sites", "[site]")
{
    SECTION("empty graph")
    {
        Graph g;
        REQUIRE_FALSE(validateSite(g).empty());
    }
    SECTION("unknown types")
    {
        Params p;
        p.seed = 3;
        Graph g = generate(p).graph;
        const NodeId stray = g.addNode("bogus");
        g.addEdge(stray, stray, "weird");
        REQUIRE(validateSite(g).size() >= 3);
    }
    SECTION("removing the first interior node cuts the reward off")
    {
        Params p;
        p.seed = 3;
        Graph g = generate(p).graph;
        NodeId mid = 0;
        for (const auto& [id, n] : g.nodes()) {
            if (n.type != kEntrance && n.type != kReward) {
                mid = id;
                break;
            }
        }
        REQUIRE(g.removeNode(mid));
        REQUIRE_FALSE(validateSite(g).empty());
    }
    SECTION("too long")
    {
        Graph g;
        NodeId prev = g.addNode(kEntrance);
        for (int i = 0; i < 30; ++i) {
            const NodeId n = g.addNode(kEncounter);
            g.addEdge(prev, n, kPath);
            prev = n;
        }
        const NodeId r = g.addNode(kReward);
        g.addEdge(prev, r, kPath);
        REQUIRE_FALSE(validateSite(g).empty());
    }
    SECTION("self loop and cycle on a valid site still terminate and validate")
    {
        Graph g;
        const NodeId e = g.addNode(kEntrance);
        const NodeId a = g.addNode(kEncounter);
        const NodeId b = g.addNode(kEncounter);
        const NodeId c = g.addNode(kEncounter);
        const NodeId d = g.addNode(kRoom);
        const NodeId r = g.addNode(kReward);
        g.addEdge(e, a, kPath);
        g.addEdge(a, b, kPath);
        g.addEdge(b, c, kPath);
        g.addEdge(c, d, kPath);
        g.addEdge(d, r, kPath);
        REQUIRE(validateSite(g).empty());
        g.addEdge(a, a, kPath);
        g.addEdge(c, a, kPath);
        REQUIRE(validateSite(g).empty());
    }
    SECTION("a shortcut does not make a node reachable")
    {
        Graph g;
        const NodeId e = g.addNode(kEntrance);
        const NodeId a = g.addNode(kEncounter);
        const NodeId b = g.addNode(kEncounter);
        const NodeId c = g.addNode(kEncounter);
        const NodeId d = g.addNode(kRoom);
        const NodeId r = g.addNode(kReward);
        g.addEdge(e, a, kPath);
        g.addEdge(a, b, kPath);
        g.addEdge(b, c, kPath);
        g.addEdge(c, d, kPath);
        g.addEdge(d, r, kShortcut);
        REQUIRE_FALSE(validateSite(g).empty());
    }
    SECTION("duplicate entrances and rewards")
    {
        Params p;
        p.seed = 3;
        Graph g = generate(p).graph;
        g.addNode(kEntrance);
        g.addNode(kReward);
        REQUIRE(validateSite(g).size() >= 2);
    }
}

TEST_CASE("archetypes differ in their encounter mix", "[site]")
{
    auto encounters = [](std::uint32_t archetype) {
        double encounterNodes = 0;
        double allNodes = 0;
        for (std::uint64_t seed = 0; seed < 200; ++seed) {
            Params p;
            p.seed = seed;
            p.archetype = archetype;
            const Result r = generate(p);
            for (const auto& [id, n] : r.graph.nodes()) {
                encounterNodes += n.type == kEncounter ? 1.0 : 0.0;
                allNodes += 1.0;
            }
        }
        return encounterNodes / allNodes;
    };
    REQUIRE(encounters(3) > encounters(0));
    REQUIRE(encounters(3) > encounters(1));
}

TEST_CASE("nodeSeconds is zero for unknown types", "[site]")
{
    REQUIRE(nodeSeconds("nope") == 0);
    REQUIRE(nodeSeconds(kEncounter) > nodeSeconds(kRoom));
}
