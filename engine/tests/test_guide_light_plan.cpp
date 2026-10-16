#include "ichor/graph/guide_light_plan.h"
#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::guidelight;

namespace {

const RoomLight* find(const Result& r, NodeId id)
{
    for (const RoomLight& l : r.rooms) {
        if (l.room == id) {
            return &l;
        }
    }
    return nullptr;
}

struct Chain {
    Graph g;
    std::vector<NodeId> n;
};

// entry -> junctions -> climax
Chain chain(int rooms)
{
    Chain c;
    c.n.push_back(c.g.addNode(mission::kEntry));
    for (int i = 1; i < rooms - 1; ++i) {
        c.n.push_back(c.g.addNode(mission::kJunction));
    }
    c.n.push_back(c.g.addNode(mission::kClimax));
    for (std::size_t i = 0; i + 1 < c.n.size(); ++i) {
        c.g.addEdge(c.n[i], c.n[i + 1], mission::kPath);
    }
    return c;
}

Params toLast(const Chain& c)
{
    Params p;
    p.objective = c.n.back();
    return p;
}

} // namespace

TEST_CASE("linear graph brightens and warms monotonically to the objective", "[guide_light]")
{
    const Chain c = chain(6);
    const Result r = plan(c.g, toLast(c));
    REQUIRE(r.error.empty());
    REQUIRE(r.guided);
    REQUIRE(r.rooms.size() == 6);
    REQUIRE(r.criticalPath == c.n);
    for (std::size_t i = 0; i + 1 < c.n.size(); ++i) {
        const RoomLight* a = find(r, c.n[i]);
        const RoomLight* b = find(r, c.n[i + 1]);
        REQUIRE(a->level < b->level);
        REQUIRE(a->warmth < b->warmth);
        REQUIRE(a->towards == c.n[i + 1]);
        REQUIRE(a->onCriticalPath);
    }
    REQUIRE(find(r, c.n.back())->level == 1.0f);
    REQUIRE(find(r, c.n.back())->towards == 0);
    REQUIRE(find(r, c.n.back())->distance == 0);
    REQUIRE(find(r, c.n.front())->level == Params{}.pathFloor);
}

TEST_CASE("branching graph: dead ends are lit but dimmer than the path and point back", "[guide_light]")
{
    Chain c = chain(5);
    const NodeId deadA = c.g.addNode(mission::kReward);
    const NodeId deadB = c.g.addNode(mission::kRest);
    const NodeId deepA = c.g.addNode(mission::kRest);
    c.g.addEdge(c.n[0], deadA, mission::kPath);
    c.g.addEdge(c.n[3], deadB, mission::kPath);
    c.g.addEdge(deadB, deepA, mission::kPath);
    const Params p = toLast(c);
    const Result r = plan(c.g, p);
    REQUIRE(r.error.empty());
    REQUIRE(r.rooms.size() == 8);

    for (const NodeId id : {deadA, deadB, deepA}) {
        const RoomLight* l = find(r, id);
        REQUIRE_FALSE(l->onCriticalPath);
        REQUIRE(l->level >= p.ambientFloor);
        REQUIRE(l->towards != 0);
    }
    REQUIRE(find(r, deadA)->towards == c.n[0]);
    REQUIRE(find(r, deepA)->towards == deadB);
    REQUIRE(find(r, deadB)->level < find(r, c.n[3])->level);
    REQUIRE(find(r, deepA)->level <= find(r, deadB)->level);
}

TEST_CASE("critical path is brighter on average than off it; red without guidance", "[guide_light]")
{
    Chain c = chain(6);
    for (int i = 0; i < 6; ++i) {
        const NodeId side = c.g.addNode(mission::kRest);
        c.g.addEdge(c.n[static_cast<std::size_t>(i)], side, mission::kPath);
    }
    Params p = toLast(c);
    const Result guided = plan(c.g, p);
    REQUIRE(guided.error.empty());
    REQUIRE(meanLevel(guided, true) > meanLevel(guided, false) + 0.1f);

    p.enabled = false;
    const Result flat = plan(c.g, p);
    REQUIRE(flat.error.empty());
    REQUIRE_FALSE(flat.guided);
    for (const RoomLight& l : flat.rooms) {
        REQUIRE(l.level == p.neutralLevel);
        REQUIRE(l.towards == 0);
    }
}

TEST_CASE("shorter route wins and a loop does not confuse the hint", "[guide_light]")
{
    Graph g;
    const NodeId e = g.addNode(mission::kEntry);
    const NodeId a = g.addNode(mission::kJunction);
    const NodeId b = g.addNode(mission::kJunction);
    const NodeId c2 = g.addNode(mission::kJunction);
    const NodeId x = g.addNode(mission::kClimax);
    g.addEdge(e, a, mission::kPath);
    g.addEdge(a, b, mission::kPath);
    g.addEdge(b, x, mission::kPath);
    g.addEdge(e, c2, mission::kPath);
    g.addEdge(c2, x, mission::kPath);
    Params p;
    p.objective = x;
    const Result r = plan(g, p);
    REQUIRE(r.error.empty());
    REQUIRE(r.criticalPath == std::vector<NodeId>{e, c2, x});
    REQUIRE(find(r, a)->towards == b);
    REQUIRE_FALSE(find(r, a)->onCriticalPath);
}

TEST_CASE("non path edges do not carry light", "[guide_light]")
{
    Graph g;
    const NodeId e = g.addNode(mission::kEntry);
    const NodeId x = g.addNode(mission::kClimax);
    g.addEdge(e, x, mission::kUnlocks);
    Params p;
    p.objective = x;
    const Result r = plan(g, p);
    REQUIRE(r.error.empty());
    REQUIRE_FALSE(r.guided);
}

TEST_CASE("unreachable objective gives neutral light for every room, not an error", "[guide_light]")
{
    Chain c = chain(4);
    const NodeId island = c.g.addNode(mission::kClimax);
    Params p;
    p.objective = island;
    const Result r = plan(c.g, p);
    REQUIRE(r.error.empty());
    REQUIRE_FALSE(r.guided);
    REQUIRE(r.criticalPath.empty());
    REQUIRE(r.rooms.size() == 5);
    for (const RoomLight& l : r.rooms) {
        REQUIRE(l.level == p.neutralLevel);
        REQUIRE(l.warmth == p.neutralWarmth);
        REQUIRE(l.towards == 0);
        REQUIRE_FALSE(l.onCriticalPath);
    }
}

TEST_CASE("entry equal to objective is guided with full light", "[guide_light]")
{
    Graph g;
    const NodeId e = g.addNode(mission::kEntry);
    const NodeId s = g.addNode(mission::kRest);
    g.addEdge(e, s, mission::kPath);
    Params p;
    p.objective = e;
    const Result r = plan(g, p);
    REQUIRE(r.error.empty());
    REQUIRE(r.guided);
    REQUIRE(find(r, e)->level == 1.0f);
    REQUIRE(find(r, s)->towards == e);
}

TEST_CASE("bad input is rejected with an error", "[guide_light]")
{
    const Chain c = chain(3);
    const Params good = toLast(c);

    REQUIRE_FALSE(plan(Graph{}, good).error.empty());

    Params p = good;
    p.objective = 999;
    REQUIRE_FALSE(plan(c.g, p).error.empty());
    p = good;
    p.objective = 0;
    REQUIRE_FALSE(plan(c.g, p).error.empty());
    p = good;
    p.entry = 999;
    REQUIRE_FALSE(plan(c.g, p).error.empty());

    Graph noEntry;
    const NodeId only = noEntry.addNode(mission::kRest);
    p = Params{};
    p.objective = only;
    REQUIRE_FALSE(plan(noEntry, p).error.empty());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, inf, -inf, -0.1f, 1.5f}) {
        p = good;
        p.pathFloor = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.ambientFloor = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.offPathFactor = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.neutralLevel = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.pathWarmthFloor = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.ambientWarmth = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
        p = good;
        p.neutralWarmth = bad;
        REQUIRE_FALSE(plan(c.g, p).error.empty());
    }

    p = good;
    p.ambientFloor = 0.9f;
    p.pathFloor = 0.5f;
    REQUIRE_FALSE(plan(c.g, p).error.empty());
    p = good;
    p.ambientWarmth = 0.9f;
    p.pathWarmthFloor = 0.5f;
    REQUIRE_FALSE(plan(c.g, p).error.empty());
}

TEST_CASE("oversized graphs are rejected, the largest allowed one works", "[guide_light]")
{
    Chain big = chain(static_cast<int>(kMaxRooms));
    const Result ok = plan(big.g, toLast(big));
    REQUIRE(ok.error.empty());
    REQUIRE(ok.rooms.size() == kMaxRooms);
    REQUIRE(ok.criticalPath.size() == kMaxRooms);

    const NodeId extra = big.g.addNode(mission::kRest);
    big.g.addEdge(big.n[0], extra, mission::kPath);
    const Result tooBig = plan(big.g, toLast(big));
    REQUIRE_FALSE(tooBig.error.empty());
    REQUIRE(tooBig.rooms.empty());
}

TEST_CASE("levels stay in range and the result is deterministic", "[guide_light]")
{
    Chain c = chain(30);
    for (std::size_t i = 0; i < c.n.size(); i += 2) {
        const NodeId a = c.g.addNode(mission::kRest);
        const NodeId b = c.g.addNode(mission::kRest);
        c.g.addEdge(c.n[i], a, mission::kPath);
        c.g.addEdge(a, b, mission::kPath);
        c.g.addEdge(b, c.n[(i + 7) % c.n.size()], mission::kPath);
    }
    const Params p = toLast(c);
    const Result r1 = plan(c.g, p);
    const Result r2 = plan(c.g, p);
    REQUIRE(r1.error.empty());
    REQUIRE(r1.rooms.size() == r2.rooms.size());
    for (std::size_t i = 0; i < r1.rooms.size(); ++i) {
        const RoomLight& l = r1.rooms[i];
        REQUIRE(l.level >= p.ambientFloor);
        REQUIRE(l.level <= 1.0f);
        REQUIRE(l.warmth >= 0.0f);
        REQUIRE(l.warmth <= 1.0f);
        REQUIRE(l.room == r2.rooms[i].room);
        REQUIRE(l.level == r2.rooms[i].level);
        REQUIRE(l.towards == r2.rooms[i].towards);
        if (i > 0) {
            REQUIRE(r1.rooms[i - 1].room < l.room);
        }
    }
}

TEST_CASE("a generated mission is guided toward its climax", "[guide_light]")
{
    const mission::Ruleset rules = mission::loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    REQUIRE(rules.error.empty());
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        mission::Params mp;
        mp.seed = seed;
        const mission::Result m = mission::generate(rules, mp);
        REQUIRE(m.error.empty());
        Params p;
        for (const auto& [id, node] : m.graph.nodes()) {
            if (node.type == mission::kClimax) {
                p.objective = id;
            }
        }
        const Result r = plan(m.graph, p);
        REQUIRE(r.error.empty());
        REQUIRE(r.guided);
        REQUIRE(meanLevel(r, true) > meanLevel(r, false));
    }
}
