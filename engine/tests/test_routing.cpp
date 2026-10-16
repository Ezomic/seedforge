#include "ichor/graph/routing.h"

#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor::graph;
using namespace ichor::graph::routing;
namespace m = ichor::graph::mission;

namespace {

const m::Ruleset& shippedRules()
{
    static const m::Ruleset rules = m::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return rules;
}

Graph generated(std::uint64_t seed, std::uint32_t locks = 2, std::uint32_t growSteps = 10)
{
    m::Params p;
    p.seed = seed;
    p.locks = locks;
    p.growSteps = growSteps;
    m::Result r = m::generate(shippedRules(), p);
    INFO(r.error);
    REQUIRE(r.error.empty());
    return std::move(r.graph);
}

struct Laid {
    Graph graph;
    std::map<NodeId, Cell> cells;
    Result routed;
};

Params routeParams()
{
    Params p;
    p.maxExtent = 24;
    p.maxFloors = 4;
    p.margin = 2;
    return p;
}

// Missions the validator and the embedder accept, with their routing.
std::vector<Laid> laidOut(std::uint64_t from, std::uint64_t count)
{
    std::vector<Laid> out;
    for (std::uint64_t seed = from; seed < from + count; ++seed) {
        Graph g = generated(seed);
        if (!dungeon::validateLayout(dungeon::fromGraph(g)).empty())
            continue;
        embedding::Params ep;
        ep.seed = seed;
        ep.maxExtent = 14;
        ep.maxFloors = 3;
        Layout3d l = layout(g, ep, routeParams(), 32);
        INFO("seed " << seed << " " << l.error);
        REQUIRE(l.error.empty());
        out.push_back({std::move(g), std::move(l.cells), std::move(l.routing)});
    }
    return out;
}

int manhattan(const Cell& a, const Cell& b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.floor - b.floor);
}

// entry -> a -> b with a one-way shortcut b -> entry and an unlock from a to b.
Graph smallWithShortcut()
{
    Graph g;
    const NodeId entry = g.addNode(m::kEntry);
    const NodeId a = g.addNode(m::kJunction);
    const NodeId b = g.addNode(m::kClimax);
    g.addEdge(entry, a, m::kPath);
    g.addEdge(a, b, m::kPath);
    g.addEdge(b, entry, m::kShortcut);
    return g;
}

}

TEST_CASE("every connection of a generated mission is routed and reachable", "[routing]")
{
    const std::vector<Laid> laid = laidOut(0, 40);
    REQUIRE(laid.size() >= 10);
    bool anyRouted = false;
    for (const Laid& l : laid) {
        CHECK(check(l.graph, l.cells, l.routed, routeParams()).empty());
        CHECK(l.routed.connections.size() == l.graph.edges().size());
        for (const Connection& c : l.routed.connections) {
            REQUIRE(c.path.size() >= 2);
            CHECK(c.path.front() == l.cells.at(c.from));
            CHECK(c.path.back() == l.cells.at(c.to));
            anyRouted = anyRouted || c.path.size() > 2;
        }
    }
    CHECK(anyRouted);
}

TEST_CASE("no two cells overlap across rooms and corridors", "[routing]")
{
    for (const Laid& l : laidOut(0, 40)) {
        std::set<Cell> seen;
        std::size_t expected = 0;
        for (const auto& [id, c] : l.cells) {
            seen.insert(c);
            ++expected;
        }
        for (const Connection& c : l.routed.connections)
            for (std::size_t i = 1; i + 1 < c.path.size(); ++i) {
                seen.insert(c.path[i]);
                ++expected;
            }
        CHECK(seen.size() == expected);
    }
}

TEST_CASE("every stair, drop and climb spans exactly one floor and nothing else changes floors", "[routing]")
{
    std::size_t floorChanges = 0;
    for (const Laid& l : laidOut(0, 40)) {
        for (const Connection& c : l.routed.connections) {
            REQUIRE(c.steps.size() + 1 == c.path.size());
            for (std::size_t i = 0; i < c.steps.size(); ++i) {
                CHECK(manhattan(c.path[i], c.path[i + 1]) == 1);
                const int dz = c.path[i + 1].floor - c.path[i].floor;
                if (c.steps[i] == Step::Walk) {
                    CHECK(dz == 0);
                } else {
                    ++floorChanges;
                    CHECK(std::abs(dz) == 1);
                    if (c.steps[i] == Step::Drop)
                        CHECK(dz == -1);
                    if (c.steps[i] == Step::Climb)
                        CHECK(dz == 1);
                }
            }
        }
    }
    CHECK(floorChanges > 0);
}

TEST_CASE("a shortcut is one way and its descents are drops", "[routing]")
{
    const Graph g = smallWithShortcut();
    embedding::Params ep;
    ep.maxFloors = 1;
    const embedding::Result e = embedding::embed(g, ep);
    INFO(e.error);
    REQUIRE(e.error.empty());
    Params p = routeParams();
    p.maxFloors = 3;
    const Result r = route(g, e.cells, p);
    INFO(r.error);
    REQUIRE(r.error.empty());
    CHECK(check(g, e.cells, r, p).empty());
    const Connection& shortcut = r.connections.back();
    CHECK(shortcut.from == 3);
    CHECK(shortcut.path.size() >= 2);
}

TEST_CASE("a one-way shortcut that drops a floor is labelled a drop", "[routing]")
{
    Graph g;
    const NodeId entry = g.addNode(m::kEntry);
    const NodeId up = g.addNode(m::kClimax);
    g.addEdge(entry, up, m::kPath);
    g.addEdge(up, entry, m::kShortcut);
    const std::map<NodeId, Cell> cells = {{entry, Cell{0, 0, 0}}, {up, Cell{0, 0, 1}}};
    Params p;
    p.maxExtent = 4;
    p.maxFloors = 2;
    p.margin = 1;
    const Result r = route(g, cells, p);
    INFO(r.error);
    REQUIRE(r.error.empty());
    const Connection& shortcut = r.connections.back();
    CHECK(std::count(shortcut.steps.begin(), shortcut.steps.end(), Step::Drop) == 1);
    CHECK(std::count(shortcut.steps.begin(), shortcut.steps.end(), Step::Stair) == 0);
    CHECK(r.connections.front().steps == std::vector<Step>{Step::Stair});
    CHECK(check(g, cells, r, p).empty());
}

TEST_CASE("routing is deterministic", "[routing]")
{
    const std::vector<Laid> a = laidOut(3, 10);
    const std::vector<Laid> b = laidOut(3, 10);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].routed.connections.size() == b[i].routed.connections.size());
        for (std::size_t k = 0; k < a[i].routed.connections.size(); ++k)
            CHECK(a[i].routed.connections[k].path == b[i].routed.connections[k].path);
    }
}

TEST_CASE("check reports tampered routings", "[routing]")
{
    std::vector<Laid> laid = laidOut(0, 40);
    const auto routedIdx = [&] {
        for (std::size_t i = 0; i < laid.size(); ++i)
            for (const Connection& c : laid[i].routed.connections)
                if (c.path.size() > 3)
                    return i;
        return laid.size();
    }();
    REQUIRE(routedIdx < laid.size());
    const Laid& base = laid[routedIdx];
    const Params p = routeParams();

    SECTION("a corridor cell moved onto a room")
    {
        Result r = base.routed;
        for (Connection& c : r.connections)
            if (c.path.size() > 3) {
                c.path[1] = base.cells.begin()->second;
                break;
            }
        CHECK_FALSE(check(base.graph, base.cells, r, p).empty());
    }
    SECTION("a gap in a corridor")
    {
        Result r = base.routed;
        for (Connection& c : r.connections)
            if (c.path.size() > 3) {
                c.path[1].x += 5;
                break;
            }
        CHECK_FALSE(check(base.graph, base.cells, r, p).empty());
    }
    SECTION("a stair spanning two floors")
    {
        Result r = base.routed;
        for (Connection& c : r.connections)
            if (c.path.size() > 3) {
                c.path[1].floor += 2;
                break;
            }
        CHECK_FALSE(check(base.graph, base.cells, r, p).empty());
    }
    SECTION("a dropped connection")
    {
        Result r = base.routed;
        r.connections.pop_back();
        CHECK_FALSE(check(base.graph, base.cells, r, p).empty());
    }
    SECTION("two corridors sharing a cell")
    {
        Result r = base.routed;
        std::vector<Connection*> routed;
        for (Connection& c : r.connections)
            if (c.path.size() > 2)
                routed.push_back(&c);
        if (routed.size() >= 2) {
            routed[1]->path[1] = routed[0]->path[1];
            CHECK_FALSE(check(base.graph, base.cells, r, p).empty());
        }
    }
}

TEST_CASE("a path link routed through extra cells is reported", "[routing]")
{
    Laid l = laidOut(0, 5).front();
    for (Connection& c : l.routed.connections)
        if (l.graph.edge(c.edge)->type == m::kPath) {
            const Cell mid{c.path.front().x, c.path.front().y, c.path.front().floor};
            c.path.insert(c.path.begin() + 1, mid);
            c.steps.insert(c.steps.begin(), Step::Walk);
            break;
        }
    CHECK_FALSE(check(l.graph, l.cells, l.routed, routeParams()).empty());
}

TEST_CASE("an unreachable room is reported", "[routing]")
{
    const Laid l = laidOut(0, 5).front();
    Result r = l.routed;
    std::erase_if(r.connections, [&](const Connection& c) { return l.graph.edge(c.edge)->type == m::kPath; });
    CHECK_FALSE(check(l.graph, l.cells, r, routeParams()).empty());
}

TEST_CASE("hostile inputs are rejected without crashing", "[routing]")
{
    const Graph g = generated(1);
    const Params p = routeParams();

    CHECK_FALSE(route(g, {}, p).error.empty());

    embedding::Params ep;
    embedding::Result e = embedding::embed(g, ep);
    REQUIRE(e.error.empty());

    SECTION("two rooms sharing a cell")
    {
        auto cells = e.cells;
        cells[cells.rbegin()->first] = cells.begin()->second;
        CHECK_FALSE(route(g, cells, p).error.empty());
    }
    SECTION("an extreme cell")
    {
        auto cells = e.cells;
        cells[cells.rbegin()->first] = Cell{2147483647, -2147483647 - 1, 2147483647};
        CHECK_FALSE(route(g, cells, p).error.empty());
    }
    SECTION("a mission shifted to the edge of the coordinate range")
    {
        Graph two;
        const NodeId a = two.addNode(m::kEntry);
        const NodeId b = two.addNode(m::kClimax);
        two.addEdge(a, b, m::kPath);
        two.addEdge(b, a, m::kShortcut);
        const std::map<NodeId, Cell> edge = {{a, Cell{2147483646, 0, 0}}, {b, Cell{2147483647, 0, 0}}};
        CHECK_FALSE(route(two, edge, p).error.empty());
        const std::map<NodeId, Cell> low = {{a, Cell{-2147483647 - 1, 0, 0}}, {b, Cell{-2147483647, 0, 0}}};
        CHECK_FALSE(route(two, low, p).error.empty());
    }
    SECTION("zero and huge limits")
    {
        Params z;
        z.maxExtent = 0;
        z.maxFloors = 0;
        z.margin = 0;
        CHECK_FALSE(route(g, e.cells, z).error.empty());
        Params big;
        big.maxExtent = 0xFFFFFFFFu;
        big.maxFloors = 0xFFFFFFFFu;
        big.margin = 0xFFFFFFFFu;
        const Result r = route(g, e.cells, big);
        if (r.error.empty())
            CHECK(check(g, e.cells, r, big).empty());
    }
    SECTION("an empty graph")
    {
        const Result r = route(Graph{}, {}, p);
        CHECK(r.connections.empty());
    }
}

TEST_CASE("a self-linking unlock is rejected", "[routing]")
{
    Graph g;
    const NodeId entry = g.addNode(m::kEntry);
    g.addEdge(entry, entry, m::kUnlocks);
    const Result r = route(g, {{entry, Cell{}}}, routeParams());
    CHECK_FALSE(r.error.empty());
}
