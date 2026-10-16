#include "ichor/graph/mission.h"
#include "ichor/graph/verticality.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::verticality;

namespace {

const mission::Ruleset& shippedRules()
{
    static const mission::Ruleset rules = mission::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return rules;
}

// entry -> a -> b -> c -> climax, all path edges
Graph chain(int rooms)
{
    Graph g;
    NodeId prev = g.addNode(mission::kEntry);
    for (int i = 0; i < rooms; ++i) {
        const NodeId n = g.addNode(mission::kChallenge);
        g.addEdge(prev, n, mission::kPath);
        prev = n;
    }
    const NodeId end = g.addNode(mission::kClimax);
    g.addEdge(prev, end, mission::kPath);
    return g;
}

Layout flat(const Graph& g)
{
    Layout layout;
    for (const auto& [id, node] : g.nodes()) {
        layout.floors[id] = 0;
    }
    return layout;
}

bool mentions(const std::vector<std::string>& v, const std::string& needle)
{
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return s.find(needle) != std::string::npos; });
}

}

TEST_CASE("a flat layout violates every vertical requirement", "[verticality]")
{
    const Graph g = chain(5);
    const auto v = check(g, flat(g), Requirements{});
    CHECK(mentions(v, "floors, needs"));
    CHECK(mentions(v, "shafts, needs"));
    CHECK(mentions(v, "changes level"));
}

TEST_CASE("a single-floor layout fails even when the requirements are minimal", "[verticality]")
{
    const Graph g = chain(3);
    const Requirements one{2, 1, 0};
    CHECK_FALSE(check(g, flat(g), one).empty());
}

TEST_CASE("enforce turns a flat chain into a layout that passes", "[verticality]")
{
    const Graph g = chain(6);
    const Requirements req{4, 3, 1};
    const auto r = enforce(g, req);
    REQUIRE(r.error.empty());
    CHECK(check(g, r.layout, req).empty());
    CHECK(check(g, flat(g), req).size() > 0);
}

TEST_CASE("enforce on generated missions passes check across seeds", "[verticality]")
{
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        mission::Params p;
        p.seed = seed;
        auto m = mission::generate(shippedRules(), p);
        REQUIRE(m.error.empty());
        const Requirements req{3, 2, 1};
        const auto r = enforce(m.graph, req);
        REQUIRE(r.error.empty());
        CHECK(check(m.graph, r.layout, req).empty());
        CHECK(enforce(m.graph, req).layout.floors == r.layout.floors);
        CHECK(enforce(m.graph, req).layout.connectors == r.layout.connectors);
    }
}

TEST_CASE("a level change on the critical path needs a two-way connector", "[verticality]")
{
    const Graph g = chain(4);
    const Requirements req{3, 2, 0};
    auto r = enforce(g, req);
    REQUIRE(r.error.empty());
    REQUIRE(check(g, r.layout, req).empty());

    for (auto& [id, c] : r.layout.connectors) {
        c = Connector::Drop;
    }
    CHECK(mentions(check(g, r.layout, req), "two-way stair or shaft"));

    for (auto& [id, c] : r.layout.connectors) {
        c = Connector::Flat;
    }
    CHECK(mentions(check(g, r.layout, req), "without a stair"));
}

TEST_CASE("connector consistency is checked", "[verticality]")
{
    const Graph g = chain(2);
    Layout layout = flat(g);
    layout.connectors[1] = Connector::Stair;
    CHECK(mentions(check(g, layout, Requirements{1, 0, 0}), "same floor"));

    layout = flat(g);
    layout.floors[2] = 3;
    layout.connectors[1] = Connector::Stair;
    CHECK(mentions(check(g, layout, Requirements{1, 0, 0}), "more than one floor"));

    layout.connectors[1] = Connector::Shaft;
    layout.connectors[2] = Connector::Shaft;
    CHECK(check(g, layout, Requirements{1, 0, 0}).empty());

    layout.connectors[1] = Connector::Drop;
    layout.connectors[2] = Connector::Shaft;
    CHECK(mentions(check(g, layout, Requirements{1, 0, 0}), "rises"));
}

TEST_CASE("empty and malformed graphs report violations without crashing", "[verticality]")
{
    const Graph empty;
    CHECK(mentions(check(empty, Layout{}, Requirements{}), "exactly one entry"));
    CHECK(criticalPath(empty).empty());
    CHECK_FALSE(enforce(empty, Requirements{}).error.empty());
    CHECK(enforce(empty, Requirements{0, 0, 0}).error.size() > 0);

    Graph disconnected;
    disconnected.addNode(mission::kEntry);
    disconnected.addNode(mission::kClimax);
    CHECK(mentions(check(disconnected, flat(disconnected), Requirements{}), "no path"));
    CHECK_FALSE(enforce(disconnected, Requirements{}).error.empty());
}

TEST_CASE("a layout naming unknown rooms or edges or omitting rooms is rejected", "[verticality]")
{
    const Graph g = chain(2);
    Layout layout = flat(g);
    layout.floors[999] = 0;
    layout.connectors[999] = Connector::Flat;
    const auto v = check(g, layout, Requirements{1, 0, 0});
    CHECK(mentions(v, "unknown room"));
    CHECK(mentions(v, "unknown edge"));

    CHECK(mentions(check(g, Layout{}, Requirements{1, 0, 0}), "has no floor"));
}

TEST_CASE("extreme floor values never overflow", "[verticality]")
{
    const Graph g = chain(2);
    Layout layout = flat(g);
    layout.floors[1] = std::numeric_limits<std::int32_t>::min();
    layout.floors[2] = std::numeric_limits<std::int32_t>::max();
    CHECK(mentions(check(g, layout, Requirements{1, 0, 0}), "out of range"));

    layout.floors[1] = -kMaxFloor;
    layout.floors[2] = kMaxFloor;
    layout.connectors[2] = Connector::Shaft;
    const auto v = check(g, layout, Requirements{1, 0, 0});
    CHECK(mentions(v, "without a stair"));
    CHECK_FALSE(mentions(v, "out of range"));
}

TEST_CASE("hostile requirements are bounded", "[verticality]")
{
    const Graph g = chain(3);
    const Requirements huge{std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()};
    CHECK(mentions(check(g, flat(g), huge), "exceed"));
    CHECK_FALSE(enforce(g, huge).error.empty());
}

TEST_CASE("enforce fails plainly when the graph is too small", "[verticality]")
{
    const Graph g = chain(1);
    CHECK(enforce(g, Requirements{5, 5, 0}).error.find("cannot hold") != std::string::npos);
    CHECK(enforce(g, Requirements{2, 1, 3}).error.find("cannot place") != std::string::npos);
    CHECK(enforce(g, Requirements{1, 0, 0}).error.empty());
}
