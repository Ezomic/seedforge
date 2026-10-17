#include "ichor/graph/quest_binding.h"

#include "ichor/graph/quest_expansion.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::questbind;

namespace {

const quest::Ruleset& shippedRules()
{
    static const quest::Ruleset rules = quest::loadRuleset(std::string(ICHOR_REPO_DATA) + "/quests/expansion_rules.json");
    return rules;
}

Graph expanded(const std::string& tension, std::uint64_t seed)
{
    quest::Params params;
    params.seed = seed;
    params.tension = tension;
    quest::Result result = quest::expand(shippedRules(), params);
    REQUIRE(result.error.empty());
    return std::move(result.graph);
}

World richWorld()
{
    World w;
    for (int i = 0; i < 6; ++i) {
        const std::string n = std::to_string(i);
        const char* region = i % 2 ? "north" : "south";
        w.entities.push_back({"faction_" + n, EntityKind::Faction, region, 0});
        w.entities.push_back({"npc_" + n, EntityKind::Npc, region, 0});
        w.entities.push_back({"site_" + n, EntityKind::Site, region, 0});
        w.entities.push_back({"road_" + n, EntityKind::Road, region, 0});
    }
    return w;
}

const Entity& entityOf(const World& w, const std::string& id)
{
    for (const Entity& e : w.entities) {
        if (e.id == id) {
            return e;
        }
    }
    FAIL("unknown entity " << id);
    return w.entities.front();
}

}

TEST_CASE("every node of every tension binds to an entity of the right kind", "[quest][bind]")
{
    const World world = richWorld();
    for (const char* tension : quest::kTensionKinds) {
        for (std::uint64_t seed = 1; seed <= 8; ++seed) {
            const Graph g = expanded(tension, seed);
            const Result r = bind(g, world, seed);
            REQUIRE(r.error.empty());
            REQUIRE(r.bindings.size() == g.nodes().size());
            for (const Binding& b : r.bindings) {
                EntityKind want{};
                if (kindFor(b.type, want)) {
                    REQUIRE(!b.entity.empty());
                    REQUIRE(entityOf(world, b.entity).kind == want);
                } else {
                    REQUIRE(b.type == quest::kWalkAway);
                    REQUIRE(b.entity.empty());
                }
            }
        }
    }
}

TEST_CASE("binding is deterministic and the seed varies the outcome", "[quest][bind]")
{
    const World world = richWorld();
    const Graph g = expanded("contested_cell", 3);
    const Result a = bind(g, world, 99);
    const Result b = bind(g, world, 99);
    REQUIRE(a.bindings.size() == b.bindings.size());
    for (std::size_t i = 0; i < a.bindings.size(); ++i) {
        REQUIRE(a.bindings[i].entity == b.bindings[i].entity);
    }
    std::set<std::string> anchors;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        anchors.insert(bind(g, world, seed).bindings.front().entity);
    }
    REQUIRE(anchors.size() > 1);
}

TEST_CASE("a quest uses distinct entities while the world has enough", "[quest][bind]")
{
    const World world = richWorld();
    const Graph g = expanded("broken_dependency", 5);
    const Result r = bind(g, world, 5);
    REQUIRE(r.error.empty());
    std::set<std::string> used;
    std::size_t bound = 0;
    for (const Binding& b : r.bindings) {
        if (!b.entity.empty()) {
            ++bound;
            used.insert(b.entity);
        }
    }
    REQUIRE(used.size() == bound);
}

TEST_CASE("a tiny world reuses entities instead of failing", "[quest][bind]")
{
    World w;
    w.entities = {{"f", EntityKind::Faction, "", 0}, {"n", EntityKind::Npc, "", 0}, {"s", EntityKind::Site, "", 0}, {"r", EntityKind::Road, "", 0}};
    Graph g;
    g.addNode("scout");
    g.addNode("escort");
    const Result r = bind(g, w, 2);
    REQUIRE(r.error.empty());
    REQUIRE(r.bindings.size() == 2);
    REQUIRE(r.bindings[0].entity == "r");
    REQUIRE(r.bindings[1].entity == "r");
}

TEST_CASE("persistent entities are preferred over fresh ones", "[quest][bind]")
{
    World w = richWorld();
    for (Entity& e : w.entities) {
        e.region.clear();
        if (e.id == "npc_4") {
            e.appearances = 5;
        }
    }
    int hits = 0;
    int npcNodes = 0;
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
        const Graph g = expanded("hostility_pair", seed);
        const Result r = bind(g, w, seed);
        REQUIRE(r.error.empty());
        for (const Binding& b : r.bindings) {
            EntityKind k{};
            if (kindFor(b.type, k) && k == EntityKind::Npc) {
                ++npcNodes;
                if (b.entity == "npc_4") {
                    ++hits;
                }
            }
        }
    }
    REQUIRE(npcNodes > 0);
    // Unused entities win first, so npc_4 is taken by the first NPC node of
    // each quest; without the appearance bonus it would win about 1 in 6.
    REQUIRE(hits >= 16);
}

TEST_CASE("nodes prefer the anchor's region", "[quest][bind]")
{
    const World world = richWorld();
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        const Graph g = expanded("pressure_front", seed);
        const Result r = bind(g, world, seed);
        REQUIRE(r.error.empty());
        const std::string anchorRegion = entityOf(world, r.bindings.front().entity).region;
        for (const Binding& b : r.bindings) {
            if (!b.entity.empty()) {
                REQUIRE(entityOf(world, b.entity).region == anchorRegion);
            }
        }
    }
}

TEST_CASE("hostile input is refused without crashing", "[quest][bind][adversarial]")
{
    const Graph g = expanded("custodian_secret", 1);

    SECTION("empty world")
    {
        REQUIRE(!bind(g, World{}, 1).error.empty());
    }
    SECTION("missing kind")
    {
        World w = richWorld();
        std::erase_if(w.entities, [](const Entity& e) { return e.kind == EntityKind::Road; });
        Graph needsRoad;
        needsRoad.addNode("scout");
        REQUIRE(!bind(needsRoad, w, 1).error.empty());
    }
    SECTION("duplicate id")
    {
        World w = richWorld();
        w.entities.push_back(w.entities.front());
        REQUIRE(!bind(g, w, 1).error.empty());
    }
    SECTION("empty id")
    {
        World w = richWorld();
        w.entities.front().id.clear();
        REQUIRE(!bind(g, w, 1).error.empty());
    }
    SECTION("id too long")
    {
        World w = richWorld();
        w.entities.front().id.assign(kMaxIdLength + 1, 'x');
        REQUIRE(!bind(g, w, 1).error.empty());
    }
    SECTION("too many entities")
    {
        World w;
        for (std::size_t i = 0; i <= kMaxEntities; ++i) {
            w.entities.push_back({"e" + std::to_string(i), EntityKind::Npc, "", 0});
        }
        REQUIRE(!bind(g, w, 1).error.empty());
    }
    SECTION("region too long")
    {
        World w = richWorld();
        w.entities.front().region.assign(kMaxIdLength + 1, 'x');
        REQUIRE(!bind(g, w, 1).error.empty());
    }
    SECTION("too many nodes")
    {
        Graph big;
        for (std::size_t i = 0; i <= kMaxNodes; ++i) {
            big.addNode("scout");
        }
        REQUIRE(!bind(big, richWorld(), 1).error.empty());
    }
    SECTION("unknown node type")
    {
        Graph bad;
        bad.addNode("not_a_beat");
        REQUIRE(!bind(bad, richWorld(), 1).error.empty());
    }
    SECTION("empty graph")
    {
        REQUIRE(!bind(Graph{}, richWorld(), 1).error.empty());
    }
    SECTION("extreme appearances still pick the most persistent")
    {
        World w = richWorld();
        for (Entity& e : w.entities) {
            e.region.clear();
        }
        w.entities[1].appearances = 0xFFFFFFFFu;
        w.entities[5].appearances = 0xFFFFFFFEu;
        const Result r = bind(g, w, 1);
        REQUIRE(r.error.empty());
        for (const Binding& b : r.bindings) {
            EntityKind k{};
            if (kindFor(b.type, k) && k == EntityKind::Npc) {
                REQUIRE(b.entity == "npc_0");
                break;
            }
        }
    }
}
