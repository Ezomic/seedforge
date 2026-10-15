#include "ichor/graph/rewrite.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace ichor::graph;

namespace {

Rule gateKeyRule()
{
    Rule r;
    r.name = "gate-key";
    r.matchNodes = {{"start"}, {"goal"}};
    r.matchEdges = {{0, 1, "link"}};
    r.addNodes = {"gate", "key"};
    r.addEdges = {{0, 2, "path"}, {2, 1, "path"}, {0, 3, "path"}, {3, 2, "unlocks"}};
    return r;
}

Rule rewardRule()
{
    Rule r;
    r.name = "reward";
    r.matchNodes = {{"gate"}, {"goal"}};
    r.matchEdges = {{0, 1, "path"}};
    r.retypes = {{1, "goal_rewarded"}};
    r.addNodes = {"reward"};
    r.addEdges = {{0, 1, "path"}, {1, 2, "holds"}};
    return r;
}

Graph questSeed()
{
    Graph g;
    const NodeId start = g.addNode("start");
    const NodeId goal = g.addNode("goal");
    g.addEdge(start, goal, "link");
    return g;
}

std::size_t countType(const Graph& g, const std::string& type)
{
    std::size_t n = 0;
    for (const auto& [id, node] : g.nodes()) {
        n += node.type == type ? 1 : 0;
    }
    return n;
}

// Two alternatives per step, so the seed decides the shape.
RuleSet growthRules()
{
    Rule room;
    room.name = "room";
    room.matchNodes = {{"hub"}};
    room.addNodes = {"room"};
    room.addEdges = {{0, 1, "link"}};
    Rule trap = room;
    trap.name = "trap";
    trap.addNodes = {"trap"};
    return {{room, trap}};
}

Graph hubOnly()
{
    Graph g;
    g.addNode("hub");
    return g;
}

}

TEST_CASE("gate-key-reward ruleset expands a link into a gated path")
{
    Graph g = questSeed();
    const RuleSet rules{{gateKeyRule(), rewardRule()}};
    const RewriteResult result = rewrite(g, rules, {.seed = 7, .maxIterations = 10});

    REQUIRE(result.reason == StopReason::FixedPoint);
    REQUIRE(result.iterations == 2);
    REQUIRE(countType(g, "gate") == 1);
    REQUIRE(countType(g, "key") == 1);
    REQUIRE(countType(g, "reward") == 1);
    REQUIRE(countType(g, "goal_rewarded") == 1);
    REQUIRE(countType(g, "goal") == 0);

    std::multiset<std::string> edgeTypes;
    for (const auto& [id, e] : g.edges()) {
        edgeTypes.insert(e.type);
    }
    REQUIRE(edgeTypes.count("link") == 0);
    REQUIRE(edgeTypes.count("path") == 3);
    REQUIRE(edgeTypes.count("unlocks") == 1);
    REQUIRE(edgeTypes.count("holds") == 1);
}

TEST_CASE("the key opens the gate and the gate leads to the goal")
{
    Graph g = questSeed();
    rewrite(g, {{gateKeyRule()}}, {.seed = 1});

    NodeId key = 0;
    NodeId gate = 0;
    for (const auto& [id, n] : g.nodes()) {
        key = n.type == "key" ? id : key;
        gate = n.type == "gate" ? id : gate;
    }
    bool unlocks = false;
    bool gateToGoal = false;
    for (const auto& [id, e] : g.edges()) {
        unlocks = unlocks || (e.type == "unlocks" && e.from == key && e.to == gate);
        gateToGoal = gateToGoal || (e.type == "path" && e.from == gate && g.node(e.to)->type == "goal");
    }
    REQUIRE(unlocks);
    REQUIRE(gateToGoal);
}

TEST_CASE("matching is injective, directed and typed")
{
    Graph g;
    const NodeId a = g.addNode("room");
    const NodeId b = g.addNode("room");
    g.addEdge(a, b, "link");
    g.addEdge(b, b, "link");

    Rule r;
    r.matchNodes = {{"room"}, {"room"}};
    r.matchEdges = {{0, 1, "link"}};

    const auto matches = findMatches(g, r);
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0] == std::vector<NodeId>{a, b});

    r.matchEdges = {{0, 1, "other"}};
    REQUIRE(findMatches(g, r).empty());
}

TEST_CASE("matches come back in ascending ID order")
{
    Graph g;
    for (int i = 0; i < 3; ++i) {
        g.addNode("room");
    }
    Rule r;
    r.matchNodes = {{"room"}, {"room"}};
    const auto matches = findMatches(g, r);
    REQUIRE(matches.size() == 6);
    REQUIRE(matches.front() == std::vector<NodeId>{1, 2});
    REQUIRE(matches.back() == std::vector<NodeId>{3, 2});
}

TEST_CASE("deleting a node removes its unmatched edges too")
{
    Graph g;
    const NodeId a = g.addNode("a");
    const NodeId b = g.addNode("b");
    const NodeId c = g.addNode("c");
    g.addEdge(a, b, "x");
    g.addEdge(b, c, "y");

    Rule r;
    r.matchNodes = {{"a"}, {"b"}};
    r.matchEdges = {{0, 1, "x"}};
    r.deleteNodes = {1};
    rewrite(g, {{r}}, {});

    REQUIRE(g.nodes().size() == 2);
    REQUIRE(g.edges().empty());
}

TEST_CASE("the iteration budget stops a rule that never runs out of matches")
{
    Graph g = hubOnly();
    const RewriteResult result = rewrite(g, growthRules(), {.seed = 3, .maxIterations = 25});
    REQUIRE(result.reason == StopReason::BudgetExhausted);
    REQUIRE(result.iterations == 25);
    REQUIRE(g.nodes().size() == 26);
    REQUIRE(result.applied.size() == 25);
}

TEST_CASE("a zero budget changes nothing")
{
    Graph g = hubOnly();
    const std::uint64_t before = g.digest();
    const RewriteResult result = rewrite(g, growthRules(), {.seed = 3, .maxIterations = 0});
    REQUIRE(result.iterations == 0);
    REQUIRE(result.digest == before);
}

TEST_CASE("the same seed gives the same graph and digest")
{
    Graph a = hubOnly();
    Graph b = hubOnly();
    const RewriteResult ra = rewrite(a, growthRules(), {.seed = 99, .maxIterations = 40});
    const RewriteResult rb = rewrite(b, growthRules(), {.seed = 99, .maxIterations = 40});
    REQUIRE(ra.digest == rb.digest);
    REQUIRE(ra.digest == a.digest());
    REQUIRE(ra.applied == rb.applied);
}

TEST_CASE("different seeds reach different graphs")
{
    std::set<std::uint64_t> digests;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        Graph g = hubOnly();
        digests.insert(rewrite(g, growthRules(), {.seed = seed, .maxIterations = 20}).digest);
    }
    REQUIRE(digests.size() >= 6);
}

TEST_CASE("rule weights bias selection")
{
    Rule common = growthRules().rules[0];
    common.weight = 9;
    Rule rare = growthRules().rules[1];
    rare.weight = 1;
    Graph g = hubOnly();
    const RewriteResult result = rewrite(g, {{common, rare}}, {.seed = 5, .maxIterations = 400});
    const auto rooms = std::count(result.applied.begin(), result.applied.end(), 0u);
    REQUIRE(rooms > 300);
    REQUIRE(rooms < 400);
}

TEST_CASE("the digest reflects types, direction and ids")
{
    Graph base = questSeed();
    const std::uint64_t digest = base.digest();

    Graph retyped = questSeed();
    retyped.node(2)->type = "boss";
    REQUIRE(retyped.digest() != digest);

    Graph reversed;
    const NodeId start = reversed.addNode("start");
    const NodeId goal = reversed.addNode("goal");
    reversed.addEdge(goal, start, "link");
    REQUIRE(reversed.digest() != digest);

    Graph again = questSeed();
    REQUIRE(again.digest() == digest);

    Graph retypedEdge;
    retypedEdge.addEdge(retypedEdge.addNode("start"), retypedEdge.addNode("goal"), "path");
    REQUIRE(retypedEdge.digest() != digest);

    Graph extraEdge = questSeed();
    extraEdge.addEdge(1, 2, "link");
    REQUIRE(extraEdge.digest() != digest);

    Graph extraNode = questSeed();
    extraNode.addNode("room");
    REQUIRE(extraNode.digest() != digest);

    Graph shiftedIds;
    shiftedIds.addNode("spare");
    shiftedIds.addNode("start");
    shiftedIds.addNode("goal");
    shiftedIds.removeNode(1);
    shiftedIds.addEdge(2, 3, "link");
    REQUIRE(shiftedIds.digest() != digest);
}

TEST_CASE("an empty graph digest is stable and depends on the id counters")
{
    Graph empty;
    Graph used;
    used.removeNode(used.addNode("x"));
    REQUIRE(empty.digest() != used.digest());
}

TEST_CASE("an empty node type matches any node; an empty edge type does not")
{
    Graph g;
    const NodeId a = g.addNode("room");
    const NodeId b = g.addNode("trap");
    g.addEdge(a, b, "link");

    Rule r;
    r.matchNodes = {{""}, {""}};
    r.matchEdges = {{0, 1, "link"}};
    REQUIRE(findMatches(g, r).size() == 1);

    r.matchEdges = {{0, 1, ""}};
    REQUIRE(findMatches(g, r).empty());
}

TEST_CASE("parallel edges: applying a rule consumes exactly one matched edge")
{
    Graph g;
    const NodeId a = g.addNode("a");
    const NodeId b = g.addNode("b");
    g.addEdge(a, b, "x");
    g.addEdge(a, b, "x");

    Rule r;
    r.matchNodes = {{"a"}, {"b"}};
    r.matchEdges = {{0, 1, "x"}};
    const auto matches = findMatches(g, r);
    REQUIRE(matches.size() == 1);

    rewrite(g, {{r}}, {.maxIterations = 1});
    REQUIRE(g.edges().size() == 1);
}

TEST_CASE("a self-loop only matches a pattern that reuses no node")
{
    Graph g;
    const NodeId a = g.addNode("room");
    g.addEdge(a, a, "link");

    Rule r;
    r.matchNodes = {{"room"}, {"room"}};
    r.matchEdges = {{0, 1, "link"}};
    REQUIRE(findMatches(g, r).empty());
}

TEST_CASE("graph edits keep the graph consistent")
{
    Graph g;
    const NodeId a = g.addNode("a");
    REQUIRE(g.addEdge(a, 99, "x") == 0);
    const NodeId b = g.addNode("b");
    const EdgeId e = g.addEdge(a, b, "x");
    REQUIRE(e != 0);
    REQUIRE(g.removeEdge(e));
    REQUIRE(!g.removeEdge(e));
    g.addEdge(a, b, "x");
    REQUIRE(g.removeNode(b));
    REQUIRE(!g.removeNode(b));
    REQUIRE(g.edges().empty());
}

TEST_CASE("the growth walk is pinned, which pins seeded selection across platforms")
{
    Graph g = hubOnly();
    const RewriteResult result = rewrite(g, growthRules(), {.seed = 12345, .maxIterations = 40});
    std::uint64_t applied = 0;
    for (std::uint32_t rule : result.applied) {
        applied = applied * 2 + rule;
    }
    REQUIRE(applied == 0xa021c51089ull);
    REQUIRE(result.digest == 0xa3d6151a315284a7ull);
}

TEST_CASE("rule weights that overflow 32 bits are rejected")
{
    Rule a = growthRules().rules[0];
    a.weight = 0xC0000000u;
    Rule b = a;
    Graph g = hubOnly();
    const RewriteResult result = rewrite(g, {{a, b}}, {});
    REQUIRE(result.reason == StopReason::InvalidRule);
}

TEST_CASE("gate-key-reward digest is pinned for cross-platform comparison")
{
    Graph g = questSeed();
    const RewriteResult result = rewrite(g, {{gateKeyRule(), rewardRule()}}, {.seed = 2024, .maxIterations = 10});
    REQUIRE(result.digest == 0xe1ac9190d1cfab9full);
}

TEST_CASE("validate names each way a rule can be malformed")
{
    Rule ok = rewardRule();
    REQUIRE(validate(ok).empty());

    Rule empty = ok;
    empty.matchNodes.clear();
    empty.matchEdges.clear();
    REQUIRE(validate(empty).find("empty pattern") != std::string::npos);

    Rule zero = ok;
    zero.weight = 0;
    REQUIRE(validate(zero).find("zero weight") != std::string::npos);

    Rule both = ok;
    both.deleteNodes = {1};
    REQUIRE(validate(both).find("also deleted") != std::string::npos);

    Rule touches = ok;
    touches.retypes.clear();
    touches.deleteNodes = {1};
    REQUIRE(validate(touches).find("deleted node") != std::string::npos);

    Rule badMatch = ok;
    badMatch.matchEdges = {{0, 5, "path"}};
    REQUIRE(validate(badMatch).find("out of range") != std::string::npos);
    REQUIRE(findMatches(questSeed(), badMatch).empty());
}

TEST_CASE("a malformed rule is reported and nothing is applied")
{
    Rule bad = rewardRule();
    bad.addEdges = {{0, 9, "holds"}};
    bad.matchNodes = {{"goal"}};
    bad.matchEdges.clear();
    Graph g = questSeed();
    const std::uint64_t before = g.digest();
    const RewriteResult result = rewrite(g, {{bad}}, {});
    REQUIRE(result.reason == StopReason::InvalidRule);
    REQUIRE(result.error.find("out of range") != std::string::npos);
    REQUIRE(g.digest() == before);
}
