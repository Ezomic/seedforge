#include "ichor/graph/hub_shortcut_plan.h"
#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::mission;
using namespace ichor::graph::hubshortcut;

namespace {

const Ruleset& shippedRules()
{
    static const Ruleset rules = loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return rules;
}

Graph generated(std::uint64_t seed, std::uint32_t growSteps = 12)
{
    Params params;
    params.seed = seed;
    params.growSteps = growSteps;
    Result result = generate(shippedRules(), params);
    REQUIRE(result.error.empty());
    REQUIRE(validateMission(result.graph).empty());
    return std::move(result.graph);
}

// entry -> hub -> gate -> c1 -> c2 -> c3 -> reward / climax, key on a side branch of the hub.
struct Tiny {
    Graph g;
    NodeId entry, hub, key, gate, c1, c2, c3, reward, climax;
    Tiny()
    {
        entry = g.addNode(kEntry);
        hub = g.addNode(kJunction);
        key = g.addNode(kKey);
        gate = g.addNode(kGate);
        c1 = g.addNode(kChallenge);
        c2 = g.addNode(kChallenge);
        c3 = g.addNode(kChallenge);
        reward = g.addNode(kReward);
        climax = g.addNode(kClimax);
        g.addEdge(entry, hub, kPath);
        g.addEdge(hub, key, kPath);
        g.addEdge(key, gate, kUnlocks);
        g.addEdge(hub, gate, kPath);
        g.addEdge(gate, c1, kPath);
        g.addEdge(c1, c2, kPath);
        g.addEdge(c2, c3, kPath);
        g.addEdge(c3, reward, kPath);
        g.addEdge(c3, climax, kPath);
    }
};

std::size_t countShortcuts(const Graph& g)
{
    return static_cast<std::size_t>(std::count_if(g.edges().begin(), g.edges().end(), [](const auto& e) { return e.second.type == kShortcut; }));
}

}

TEST_CASE("an empty or invalid mission graph is an error, not a crash", "[dungeon][hub][hostile]")
{
    CHECK_FALSE(addHubShortcuts(Graph{}, HubParams{}).error.empty());
    Graph lone;
    lone.addNode(kEntry);
    const Plan plan = addHubShortcuts(lone, HubParams{});
    CHECK_FALSE(plan.error.empty());
    CHECK(plan.shortcuts.empty());
}

TEST_CASE("a tiny mission gets a hub and a locked shortcut from the far side", "[dungeon][hub]")
{
    Tiny t;
    const Plan plan = addHubShortcuts(t.g, HubParams{});
    REQUIRE(plan.error.empty());
    CHECK(std::count(plan.hubs.begin(), plan.hubs.end(), t.hub) == 1);
    REQUIRE_FALSE(plan.shortcuts.empty());
    for (const HubShortcut& s : plan.shortcuts) {
        const Edge* edge = plan.graph.edge(s.edge);
        REQUIRE(edge != nullptr);
        CHECK(edge->type == kShortcut);
        CHECK(edge->from == s.farSide);
        CHECK(edge->to == s.hub);
        CHECK(std::count(plan.hubs.begin(), plan.hubs.end(), s.hub) == 1);
    }
    CHECK(validateMission(plan.graph).empty());
    CHECK(countShortcuts(plan.graph) == countShortcuts(t.g) + plan.shortcuts.size());
}

TEST_CASE("the input graph is never modified", "[dungeon][hub]")
{
    Tiny t;
    const std::uint64_t before = t.g.digest();
    addHubShortcuts(t.g, HubParams{});
    CHECK(t.g.digest() == before);
}

TEST_CASE("repeated runs with the same seed give identical output", "[dungeon][hub][determinism]")
{
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const Graph g = generated(seed);
        HubParams params;
        params.seed = seed * 7 + 1;
        const Plan a = addHubShortcuts(g, params);
        const Plan b = addHubShortcuts(g, params);
        REQUIRE(a.error.empty());
        CHECK(a.graph.digest() == b.graph.digest());
        CHECK(a.hubs == b.hubs);
        REQUIRE(a.shortcuts.size() == b.shortcuts.size());
        for (std::size_t i = 0; i < a.shortcuts.size(); ++i) {
            CHECK(a.shortcuts[i].edge == b.shortcuts[i].edge);
        }
    }
}

TEST_CASE("different seeds choose different shortcuts", "[dungeon][hub][determinism]")
{
    const Graph g = generated(3, 24);
    std::set<std::uint64_t> digests;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        HubParams params;
        params.seed = seed;
        digests.insert(addHubShortcuts(g, params).graph.digest());
    }
    CHECK(digests.size() > 3);
}

TEST_CASE("generated missions get shortcuts on average and stay valid", "[dungeon][hub]")
{
    std::size_t total = 0;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        HubParams params;
        params.seed = seed;
        const Plan plan = addHubShortcuts(generated(seed), params);
        REQUIRE(plan.error.empty());
        CHECK(validateMission(plan.graph).empty());
        total += plan.shortcuts.size();
    }
    // Mean of at least one per mission; goes red if the feature adds nothing.
    CHECK(total >= 40);
}

TEST_CASE("a shortcut that would bypass a gate is refused", "[dungeon][hub]")
{
    // entry -> j; j -> key; key unlocks gate; j -> gate -> m -> reward / climax;
    // j -> p1 -> p2 -> p3 -> far is the long way round, open with the gate shut.
    Graph h;
    const NodeId entry = h.addNode(kEntry);
    const NodeId j = h.addNode(kJunction);
    const NodeId key = h.addNode(kKey);
    const NodeId gate = h.addNode(kGate);
    const NodeId m = h.addNode(kChallenge);
    const NodeId reward = h.addNode(kReward);
    const NodeId climax = h.addNode(kClimax);
    h.addEdge(entry, j, kPath);
    h.addEdge(j, key, kPath);
    h.addEdge(key, gate, kUnlocks);
    h.addEdge(j, gate, kPath);
    h.addEdge(gate, m, kPath);
    h.addEdge(m, reward, kPath);
    h.addEdge(m, climax, kPath);
    NodeId prev = j;
    NodeId far = 0;
    for (int i = 0; i < 3; ++i) {
        far = h.addNode(kChallenge);
        h.addEdge(prev, far, kPath);
        prev = far;
    }
    REQUIRE(validateMission(h).empty());
    CHECK(refuseShortcut(h, far, j).empty());

    // m becomes an ancestor of far while far stays reachable around the gate:
    // a shortcut far -> m would reach m (and the climax) with the gate shut.
    h.addEdge(m, far, kPath);
    CHECK_FALSE(refuseShortcut(h, far, m).empty());
    CHECK_FALSE(refuseShortcut(h, far, gate).empty());
}

TEST_CASE("refuseShortcut rejects forward, self, duplicate and unknown links", "[dungeon][hub][hostile]")
{
    Tiny t;
    CHECK_FALSE(refuseShortcut(t.g, t.c3, t.c3).empty());
    CHECK_FALSE(refuseShortcut(t.g, t.hub, t.c3).empty());
    CHECK_FALSE(refuseShortcut(t.g, 9999, t.hub).empty());
    CHECK_FALSE(refuseShortcut(t.g, t.c3, 9999).empty());
    CHECK_FALSE(refuseShortcut(t.g, t.c3, 0).empty());
    CHECK(refuseShortcut(t.g, t.c3, t.hub).empty());
    t.g.addEdge(t.c3, t.hub, kShortcut);
    CHECK_FALSE(refuseShortcut(t.g, t.c3, t.hub).empty());
}

TEST_CASE("the plan respects hub and shortcut limits", "[dungeon][hub]")
{
    const Graph g = generated(5, 40);
    HubParams params;
    params.maxHubs = 1;
    params.shortcutsPerHub = 1;
    const Plan small = addHubShortcuts(g, params);
    REQUIRE(small.error.empty());
    CHECK(small.hubs.size() <= 1);
    CHECK(small.shortcuts.size() <= 1);

    params.maxHubs = std::numeric_limits<std::uint32_t>::max();
    params.shortcutsPerHub = std::numeric_limits<std::uint32_t>::max();
    const Plan big = addHubShortcuts(g, params);
    REQUIRE(big.error.empty());
    CHECK(big.hubs.size() <= kMaxHubs);
    CHECK(big.shortcuts.size() <= static_cast<std::size_t>(kMaxHubs) * kMaxShortcutsPerHub);

    params.maxHubs = 0;
    const Plan none = addHubShortcuts(g, params);
    REQUIRE(none.error.empty());
    CHECK(none.shortcuts.empty());
    CHECK(none.graph.digest() == g.digest());
}

TEST_CASE("an oversized graph is refused", "[dungeon][hub][hostile]")
{
    Tiny t;
    for (std::size_t i = 0; i <= kMaxNodes; ++i) {
        t.g.addNode(kChallenge);
    }
    CHECK_FALSE(addHubShortcuts(t.g, HubParams{}).error.empty());
}

TEST_CASE("shortcuts are one-way and stay locked until opened from the far side", "[dungeon][hub]")
{
    Tiny t;
    const Plan plan = addHubShortcuts(t.g, HubParams{});
    REQUIRE_FALSE(plan.shortcuts.empty());
    const HubShortcut s = plan.shortcuts.front();
    UnlockState state;

    CHECK_FALSE(canTraverse(plan, state, s.farSide, s.hub));
    CHECK_FALSE(canTraverse(plan, state, s.hub, s.farSide));

    CHECK_FALSE(unlock(plan, state, s.edge, s.hub));
    CHECK_FALSE(unlock(plan, state, s.edge, 9999));
    CHECK_FALSE(unlock(plan, state, 9999, s.farSide));
    CHECK(state.open.empty());

    CHECK(unlock(plan, state, s.edge, s.farSide));
    CHECK_FALSE(unlock(plan, state, s.edge, s.farSide));
    CHECK(canTraverse(plan, state, s.farSide, s.hub));
    CHECK_FALSE(canTraverse(plan, state, s.hub, s.farSide));
}
