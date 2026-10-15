#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::mission;

namespace {

const Ruleset& shippedRules()
{
    static const Ruleset rules = loadRuleset(std::string(ICHOR_REPO_DATA) + "/dungeon_missions/mission_rules.json");
    return rules;
}

Graph generated(std::uint64_t seed, std::uint32_t locks = 2, std::uint32_t growSteps = 6)
{
    Params params;
    params.seed = seed;
    params.locks = locks;
    params.growSteps = growSteps;
    Result result = generate(shippedRules(), params);
    REQUIRE(result.error.empty());
    return std::move(result.graph);
}

std::size_t countType(const Graph& g, const std::string& type)
{
    return static_cast<std::size_t>(std::count_if(g.nodes().begin(), g.nodes().end(), [&](const auto& n) { return n.second.type == type; }));
}

std::size_t countEdges(const Graph& g, const std::string& type)
{
    return static_cast<std::size_t>(std::count_if(g.edges().begin(), g.edges().end(), [&](const auto& e) { return e.second.type == type; }));
}

bool hasError(const std::vector<std::string>& errors, const std::string& fragment)
{
    return std::any_of(errors.begin(), errors.end(), [&](const std::string& e) { return e.find(fragment) != std::string::npos; });
}

// entry -> gate -> reward / climax, key on a side branch.
Graph handBuilt()
{
    Graph g;
    const NodeId entry = g.addNode(kEntry);
    const NodeId junction = g.addNode(kJunction);
    const NodeId key = g.addNode(kKey);
    const NodeId gate = g.addNode(kGate);
    const NodeId reward = g.addNode(kReward);
    const NodeId climax = g.addNode(kClimax);
    g.addEdge(entry, junction, kPath);
    g.addEdge(junction, key, kPath);
    g.addEdge(key, gate, kUnlocks);
    g.addEdge(junction, gate, kPath);
    g.addEdge(gate, reward, kPath);
    g.addEdge(gate, climax, kPath);
    return g;
}

}

TEST_CASE("the shipped mission rules load", "[dungeon]")
{
    REQUIRE(shippedRules().error.empty());
    CHECK_FALSE(shippedRules().core.rules.empty());
    CHECK_FALSE(shippedRules().resolve.rules.empty());
}

TEST_CASE("the same seed builds the same mission", "[dungeon][determinism]")
{
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        CHECK(generated(seed).digest() == generated(seed).digest());
    }
}

TEST_CASE("different seeds build different missions", "[dungeon][determinism]")
{
    std::set<std::uint64_t> digests;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        digests.insert(generated(seed).digest());
    }
    CHECK(digests.size() > 25);
}

TEST_CASE("every generated mission satisfies the structural invariants", "[dungeon][invariants]")
{
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const std::uint32_t locks = static_cast<std::uint32_t>(seed % 5);
        const std::uint32_t grow = static_cast<std::uint32_t>(seed % 17);
        const Graph g = generated(seed * 7919 + 1, locks, grow);
        const auto errors = validateMission(g);
        INFO("seed " << seed << ": " << (errors.empty() ? "" : errors.front()));
        CHECK(errors.empty());
    }
}

TEST_CASE("every mission has the whole vocabulary", "[dungeon][invariants]")
{
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const Graph g = generated(seed, 0, 0);
        INFO("seed " << seed);
        CHECK(countType(g, kEntry) == 1);
        CHECK(countType(g, kClimax) == 1);
        CHECK(countType(g, kGate) >= 1);
        CHECK(countType(g, kKey) == countType(g, kGate));
        CHECK(countType(g, kReward) >= 1);
        CHECK(countType(g, kReveal) >= 1);
        CHECK(countEdges(g, kShortcut) >= 1);
        CHECK(countEdges(g, kUnlocks) == countType(g, kGate));
        CHECK(countType(g, kSegment) == 0);
        CHECK(countType(g, kVault) == 0);
    }
}

TEST_CASE("more steps build bigger missions", "[dungeon]")
{
    std::size_t small = 0;
    std::size_t large = 0;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        small += generated(seed, 0, 0).nodes().size();
        large += generated(seed, 8, 32).nodes().size();
    }
    CHECK(large > small * 2);
}

TEST_CASE("absurd parameters are clamped, not obeyed", "[dungeon][hostile]")
{
    Params params;
    params.seed = 9;
    params.locks = std::numeric_limits<std::uint32_t>::max();
    params.growSteps = std::numeric_limits<std::uint32_t>::max();
    const Result result = generate(shippedRules(), params);
    REQUIRE(result.error.empty());
    CHECK(result.graph.nodes().size() < 2000);
    CHECK(validateMission(result.graph).empty());
}

TEST_CASE("the validator rejects a reward that is not behind a gate", "[dungeon][invariants]")
{
    REQUIRE(validateMission(handBuilt()).empty());

    Graph g = handBuilt();
    const NodeId reward = [&] {
        for (const auto& [id, node] : g.nodes()) {
            if (node.type == kReward) {
                return id;
            }
        }
        return NodeId{0};
    }();
    g.addEdge(2, reward, kPath); // the junction (node 2) now leads straight to the reward
    CHECK(hasError(validateMission(g), "reward"));
}

TEST_CASE("removing the gating from a generated mission turns the validator red", "[dungeon][invariants]")
{
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        Graph g = generated(seed);
        REQUIRE(validateMission(g).empty());
        for (NodeId id = 1; id <= g.nodes().rbegin()->first; ++id) {
            if (Node* node = g.node(id); node != nullptr && node->type == kGate) {
                node->type = kChallenge;
            }
        }
        const auto errors = validateMission(g);
        INFO("seed " << seed);
        CHECK(hasError(errors, "without passing a gate"));
    }
}

TEST_CASE("the validator rejects a gate with no key", "[dungeon][invariants]")
{
    Graph g = handBuilt();
    for (const auto& [id, edge] : g.edges()) {
        if (edge.type == kUnlocks) {
            g.removeEdge(id);
            break;
        }
    }
    CHECK(hasError(validateMission(g), "has no key"));
}

TEST_CASE("the validator rejects a key behind its own gate", "[dungeon][invariants]")
{
    Graph g;
    const NodeId entry = g.addNode(kEntry);
    const NodeId gate = g.addNode(kGate);
    const NodeId key = g.addNode(kKey);
    const NodeId climax = g.addNode(kClimax);
    g.addEdge(entry, gate, kPath);
    g.addEdge(gate, key, kPath);
    g.addEdge(key, gate, kUnlocks);
    g.addEdge(gate, climax, kPath);
    CHECK(hasError(validateMission(g), "unreachable"));
}

TEST_CASE("the validator rejects a shortcut that leads forward", "[dungeon][invariants]")
{
    Graph g = handBuilt();
    g.addEdge(1, 6, kShortcut); // entry straight to the climax
    CHECK(hasError(validateMission(g), "shortcut"));
}

TEST_CASE("the validator rejects leftover placeholders and a missing climax", "[dungeon][invariants]")
{
    Graph g = handBuilt();
    g.addNode(kSegment);
    CHECK(hasError(validateMission(g), "unresolved"));

    Graph empty;
    CHECK(hasError(validateMission(empty), "exactly one entry"));
}

TEST_CASE("the validator survives self referencing edges and cycles", "[dungeon][hostile]")
{
    Graph g = handBuilt();
    g.addEdge(1, 1, kPath);
    g.addEdge(6, 1, kShortcut);
    g.addEdge(3, 3, kUnlocks);
    g.addEdge(6, 2, kPath);
    g.addEdge(2, 6, kShortcut);
    const auto errors = validateMission(g);
    CHECK(hasError(errors, "shortcut"));
}

TEST_CASE("the validator rejects a shortcut into a sibling branch", "[dungeon][invariants]")
{
    Graph g;
    const NodeId entry = g.addNode(kEntry);
    const NodeId junction = g.addNode(kJunction);
    const NodeId a = g.addNode(kChallenge);
    const NodeId b = g.addNode(kChallenge);
    const NodeId deep = g.addNode(kChallenge);
    const NodeId key = g.addNode(kKey);
    const NodeId gate = g.addNode(kGate);
    const NodeId climax = g.addNode(kClimax);
    g.addEdge(entry, junction, kPath);
    g.addEdge(junction, a, kPath);
    g.addEdge(junction, b, kPath);
    g.addEdge(a, deep, kPath);
    g.addEdge(deep, key, kPath);
    g.addEdge(key, gate, kUnlocks);
    g.addEdge(b, gate, kPath);
    g.addEdge(gate, climax, kPath);
    REQUIRE(validateMission(g).empty());
    g.addEdge(deep, b, kShortcut);
    CHECK(hasError(validateMission(g), "ancestor"));
}

TEST_CASE("a pinned seed builds a pinned mission on every platform", "[dungeon][determinism]")
{
    CHECK(generated(1).digest() == 0xbbd58c478ef8afffull);
}

TEST_CASE("hostile ruleset documents are rejected without crashing", "[dungeon][hostile]")
{
    const std::string good = R"({"id":"a","phase":"core","match_nodes":["seg"],"retypes":["0:x"]})";
    const auto withPhases = [&](const std::string& record) {
        std::string doc = "[" + record;
        for (const char* phase : {"lock", "grow", "reveal", "resolve"}) {
            doc += std::string(R"(,{"id":")") + phase + R"(","phase":")" + phase + R"(","match_nodes":["seg"],"retypes":["0:x"]})";
        }
        return doc + "]";
    };
    REQUIRE(parseRuleset(withPhases(good)).error.empty());

    CHECK_FALSE(parseRuleset("").error.empty());
    CHECK_FALSE(parseRuleset("not json").error.empty());
    CHECK_FALSE(parseRuleset("{}").error.empty());
    CHECK_FALSE(parseRuleset("[]").error.empty());
    CHECK_FALSE(parseRuleset("[1,2]").error.empty());
    CHECK_FALSE(parseRuleset(std::string("[\x00\xff\xfe]", 5)).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"phase":"core"})")).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"","phase":"core"})")).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"a"})")).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"a","phase":"nope","match_nodes":["seg"]})")).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"lock","phase":"core","match_nodes":["seg"]})")).error.empty());
    for (const char* weight : {"0", "-1", "1.5", "70000", "\"3\"", "1e30", "null"}) {
        CHECK_FALSE(parseRuleset(withPhases(std::string(R"({"id":"a","phase":"core","match_nodes":["seg"],"weight":)") + weight + "}")).error.empty());
    }
    for (const char* field : {R"("match_edges":["0>1"])", R"("match_edges":["x>1:path"])", R"("match_edges":["0>99999:path"])",
             R"("match_edges":["0>1:path"])", R"("add_edges":["0>7:path"])", R"("retypes":["9:x"])", R"("retypes":["nocolon"])",
             R"("delete_nodes":["-1"])", R"("delete_nodes":["12345678901234567890"])", R"("match_nodes":"seg")", R"("match_nodes":[1])",
             R"("add_nodes":{"a":1})"}) {
        CHECK_FALSE(parseRuleset(withPhases(std::string(R"({"id":"a","phase":"core","match_nodes":["seg"],)") + field + "}")).error.empty());
    }
    CHECK_FALSE(parseRuleset(withPhases(good + "," + good)).error.empty()); // duplicate id
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"a","phase":"core","match_nodes":["","",""]})")).error.empty()); // unconnected wildcards
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"a","phase":"core","match_nodes":["a","b","c","d","e"],"match_edges":["0>1:p","1>2:p","2>3:p","3>4:p"]})")).error.empty());
    CHECK_FALSE(parseRuleset(withPhases(R"({"id":"a","phase":"core","match_nodes":["a"],"add_nodes":["x","x","x","x","x","x","x","x","x"]})")).error.empty());
}

TEST_CASE("an oversized ruleset is rejected", "[dungeon][hostile]")
{
    nlohmann::json doc = nlohmann::json::array();
    for (int i = 0; i < 300; ++i) {
        doc.push_back({{"id", "r" + std::to_string(i)}, {"phase", "core"}, {"match_nodes", {"seg"}}, {"retypes", {"0:x"}}});
    }
    CHECK_FALSE(parseRuleset(doc.dump()).error.empty());

    nlohmann::json list = nlohmann::json::array();
    for (int i = 0; i < 100; ++i) {
        list.push_back("seg");
    }
    const nlohmann::json one = nlohmann::json::array({{{"id", "a"}, {"phase", "core"}, {"match_nodes", list}}});
    CHECK_FALSE(parseRuleset(one.dump()).error.empty());
}

TEST_CASE("a missing ruleset file is an error and cannot generate", "[dungeon][hostile]")
{
    const Ruleset rules = loadRuleset("no/such/file.json");
    CHECK_FALSE(rules.error.empty());
    CHECK_FALSE(generate(rules, Params{}).error.empty());
}

TEST_CASE("rules that cannot resolve every placeholder fail generation", "[dungeon][hostile]")
{
    // resolve only handles vaults, so the seg nodes the core rule leaves behind survive.
    const std::string doc = R"([
      {"id":"c","phase":"core","match_nodes":["seg",""],"match_edges":["0>1:path"],"retypes":["0:junction"],"add_nodes":["seg"],"add_edges":["0>2:path","2>1:path"]},
      {"id":"l","phase":"lock","match_nodes":["seg"],"retypes":["0:seg"]},
      {"id":"g","phase":"grow","match_nodes":["seg"],"retypes":["0:seg"]},
      {"id":"r","phase":"reveal","match_nodes":["seg"],"retypes":["0:seg"]},
      {"id":"v","phase":"resolve","match_nodes":["vault"],"retypes":["0:reward"]}])";
    const Ruleset rules = parseRuleset(doc);
    REQUIRE(rules.error.empty());
    const Result result = generate(rules, Params{});
    CHECK(result.error.empty());
    CHECK(hasError(validateMission(result.graph), "unresolved"));
}
