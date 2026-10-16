#include "ichor/graph/embedding.h"
#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace ichor::graph;
using namespace ichor::graph::embedding;
namespace m = ichor::graph::mission;

namespace {

const m::Ruleset& shippedRules()
{
    static const m::Ruleset rules = m::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return rules;
}

Graph generated(std::uint64_t seed, std::uint32_t locks = 2, std::uint32_t growSteps = 6)
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

// Mission graphs of at least minRooms rooms that the layout validator accepts
// (the key cap rejects the most heavily locked ones), for seeds from..from+count.
std::vector<Graph> largeValid(std::uint64_t from, std::uint64_t count, std::size_t minRooms)
{
    std::vector<Graph> out;
    for (std::uint64_t seed = from; seed < from + count; ++seed) {
        Graph g = generated(seed, 2, 20);
        if (g.nodes().size() >= minRooms && dungeon::validateLayout(dungeon::fromGraph(g)).empty())
            out.push_back(std::move(g));
    }
    return out;
}

// entry -> junction -> key / gate -> reward / climax; key unlocks the gate.
Graph handBuilt()
{
    Graph g;
    const NodeId entry = g.addNode(m::kEntry);
    const NodeId junction = g.addNode(m::kJunction);
    const NodeId key = g.addNode(m::kKey);
    const NodeId gate = g.addNode(m::kGate);
    const NodeId reward = g.addNode(m::kReward);
    const NodeId climax = g.addNode(m::kClimax);
    g.addEdge(entry, junction, m::kPath);
    g.addEdge(junction, key, m::kPath);
    g.addEdge(key, gate, m::kUnlocks);
    g.addEdge(junction, gate, m::kPath);
    g.addEdge(gate, reward, m::kPath);
    g.addEdge(gate, climax, m::kPath);
    return g;
}

Params params(std::uint64_t seed = 1, std::uint32_t extent = 24, std::uint32_t floors = 4)
{
    Params p;
    p.seed = seed;
    p.maxExtent = extent;
    p.maxFloors = floors;
    return p;
}

std::int32_t span(const Result& r, std::int32_t Cell::*axis)
{
    std::int32_t lo = 0, hi = 0;
    for (const auto& [id, c] : r.cells) {
        lo = std::min(lo, c.*axis);
        hi = std::max(hi, c.*axis);
    }
    return hi - lo + 1;
}

}

TEST_CASE("a tiny mission embeds with touching path edges", "[embedding]")
{
    const Graph g = handBuilt();
    const Result r = embed(g, params());
    INFO(r.error);
        REQUIRE(r.error.empty());
    CHECK(r.cells.size() == g.nodes().size());
    CHECK(r.cells.begin()->second == Cell{});
    CHECK(check(g, r.cells, params()).empty());
    std::set<Cell> distinct;
    for (const auto& [id, c] : r.cells)
        distinct.insert(c);
    CHECK(distinct.size() == r.cells.size());
    for (const auto& [id, e] : g.edges()) {
        if (e.type != m::kPath)
            continue;
        const Cell a = r.cells.at(e.from);
        const Cell b = r.cells.at(e.to);
        CHECK(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.floor - b.floor) == 1);
    }
}

TEST_CASE("the embedded layout is accepted by the layout validator", "[embedding]")
{
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const Graph g = generated(seed);
        const Result r = embed(g, params(seed));
        INFO(r.error);
        REQUIRE(r.error.empty());
        CHECK(dungeon::validateLayout(r.layout).empty());
        CHECK(r.layout.rooms.size() == g.nodes().size());
    }
}

TEST_CASE("large generated missions embed within the footprint", "[embedding]")
{
    const std::vector<Graph> graphs = largeValid(0, 60, 40);
    REQUIRE(graphs.size() >= 5);
    std::uint64_t seed = 0;
    for (const Graph& g : graphs) {
        const Params p = params(++seed, 16, 4);
        const Result r = embed(g, p);
        INFO(r.error);
        REQUIRE(r.error.empty());
        CHECK(r.cells.size() == g.nodes().size());
        CHECK(check(g, r.cells, p).empty());
        CHECK(span(r, &Cell::x) <= 16);
        CHECK(span(r, &Cell::y) <= 16);
        CHECK(span(r, &Cell::floor) <= 4);
    }
}

TEST_CASE("embedding repeats exactly and the seed matters", "[embedding][determinism]")
{
    const std::vector<Graph> graphs = largeValid(0, 60, 40);
    REQUIRE_FALSE(graphs.empty());
    const Graph& g = graphs.front();
    const Result a = embed(g, params(7));
    const Result b = embed(g, params(7));
    INFO(a.error);
    REQUIRE(a.error.empty());
    CHECK(a.cells == b.cells);
    bool differs = false;
    for (std::uint64_t seed = 8; seed < 16 && !differs; ++seed)
        differs = embed(g, params(seed)).cells != a.cells;
    CHECK(differs);
}

TEST_CASE("a single floor embedding stays flat", "[embedding]")
{
    const Graph g = generated(5);
    const Result r = embed(g, params(1, 24, 1));
    INFO(r.error);
        REQUIRE(r.error.empty());
    for (const auto& [id, c] : r.cells)
        CHECK(c.floor == 0);
}

TEST_CASE("multiple floors are used when available", "[embedding]")
{
    std::size_t withVertical = 0;
    std::uint64_t seed = 0;
    for (const Graph& g : largeValid(0, 60, 40)) {
        const Result r = embed(g, params(++seed, 24, 4));
        INFO(r.error);
        REQUIRE(r.error.empty());
        if (span(r, &Cell::floor) > 1)
            ++withVertical;
    }
    CHECK(withVertical > 0);
}

TEST_CASE("a graph that cannot fit is refused cleanly", "[embedding]")
{
    const std::vector<Graph> graphs = largeValid(0, 60, 40);
    REQUIRE_FALSE(graphs.empty());
    const Graph& g = graphs.front();
    const Result tight = embed(g, params(1, 1, 1));
    CHECK_FALSE(tight.error.empty());
    CHECK(tight.cells.empty());
    CHECK(tight.layout.rooms.empty());

    Graph star;
    const NodeId entry = star.addNode(m::kEntry);
    for (int i = 0; i < 7; ++i)
        star.addEdge(entry, star.addNode(m::kChallenge), m::kPath);
    const NodeId climax = star.addNode(m::kClimax);
    star.addEdge(entry, climax, m::kPath);
    CHECK_FALSE(embed(star, params()).error.empty());
}

TEST_CASE("an odd cycle of path edges cannot embed", "[embedding]")
{
    Graph g;
    const NodeId entry = g.addNode(m::kEntry);
    const NodeId a = g.addNode(m::kJunction);
    const NodeId climax = g.addNode(m::kClimax);
    g.addEdge(entry, a, m::kPath);
    g.addEdge(a, climax, m::kPath);
    g.addEdge(entry, climax, m::kPath);
    const Result r = embed(g, params());
    CHECK_FALSE(r.error.empty());
    CHECK(r.cells.empty());
}

TEST_CASE("hostile and empty input is refused", "[embedding]")
{
    CHECK_FALSE(embed(Graph{}, params()).error.empty());

    Graph noClimax;
    noClimax.addNode(m::kEntry);
    CHECK_FALSE(embed(noClimax, params()).error.empty());

    Graph placeholder = handBuilt();
    placeholder.addEdge(1, placeholder.addNode(m::kSegment), m::kPath);
    CHECK_FALSE(embed(placeholder, params()).error.empty());

    Graph huge;
    NodeId prev = huge.addNode(m::kEntry);
    for (std::size_t i = 0; i < dungeon::kMaxRooms + 10; ++i) {
        const NodeId next = huge.addNode(m::kChallenge);
        huge.addEdge(prev, next, m::kPath);
        prev = next;
    }
    CHECK_FALSE(embed(huge, params()).error.empty());

    Graph islands = handBuilt();
    const NodeId lone = islands.addNode(m::kRest);
    islands.addEdge(lone, 6, m::kShortcut);
    CHECK_FALSE(embed(islands, params()).error.empty());
}

TEST_CASE("extreme parameters are clamped", "[embedding]")
{
    const Graph g = handBuilt();
    Params p;
    p.maxExtent = 0;
    p.maxFloors = 0;
    CHECK(embed(g, p).error.empty() == false);
    p.maxExtent = 0xFFFFFFFFu;
    p.maxFloors = 0xFFFFFFFFu;
    const Result r = embed(g, p);
    INFO(r.error);
        REQUIRE(r.error.empty());
    CHECK(check(g, r.cells, p).empty());
}

TEST_CASE("check flags a broken embedding", "[embedding]")
{
    const Graph g = handBuilt();
    Result r = embed(g, params());
    INFO(r.error);
        REQUIRE(r.error.empty());

    auto moved = r.cells;
    moved.begin()->second = Cell{50, 50, 0};
    CHECK_FALSE(check(g, moved, params()).empty());

    auto overlapping = r.cells;
    overlapping[2] = overlapping[1];
    CHECK_FALSE(check(g, overlapping, params()).empty());

    auto missing = r.cells;
    missing.erase(3);
    CHECK_FALSE(check(g, missing, params()).empty());

    auto extra = r.cells;
    extra[999] = Cell{9, 9, 0};
    CHECK_FALSE(check(g, extra, params()).empty());

    auto huge = r.cells;
    huge[1] = Cell{INT32_MAX, INT32_MIN, 0};
    CHECK_FALSE(check(g, huge, params()).empty());
}
