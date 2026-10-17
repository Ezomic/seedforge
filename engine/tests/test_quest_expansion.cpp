#include "ichor/graph/quest_expansion.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::quest;

namespace {

const Ruleset& shippedRules()
{
    static const Ruleset rules = loadRuleset(std::string(ICHOR_REPO_DATA) + "/quests/expansion_rules.json");
    return rules;
}

Graph expanded(const std::string& tension, std::uint64_t seed)
{
    Params params;
    params.seed = seed;
    params.tension = tension;
    Result result = expand(shippedRules(), params);
    REQUIRE(result.error.empty());
    return std::move(result.graph);
}

std::size_t countType(const Graph& g, const std::string& type)
{
    return static_cast<std::size_t>(std::count_if(g.nodes().begin(), g.nodes().end(), [&](const auto& n) { return n.second.type == type; }));
}

// Longest chain of edges from the situation; the graphs are acyclic.
std::size_t depth(const Graph& g, NodeId at, std::map<NodeId, std::size_t>& memo)
{
    if (const auto found = memo.find(at); found != memo.end()) {
        return found->second;
    }
    std::size_t best = 0;
    for (const auto& [id, edge] : g.edges()) {
        if (edge.from == at) {
            best = std::max(best, 1 + depth(g, edge.to, memo));
        }
    }
    return memo[at] = best;
}

NodeId situationOf(const Graph& g)
{
    for (const auto& [id, node] : g.nodes()) {
        if (node.type == kSituation) {
            return id;
        }
    }
    return 0;
}

bool hasError(const std::vector<std::string>& errors, const std::string& fragment)
{
    return std::any_of(errors.begin(), errors.end(), [&](const std::string& e) { return e.find(fragment) != std::string::npos; });
}

std::string oneRule(const std::string& phase, const std::string& id = "r")
{
    return R"([{"id": ")" + id + R"(", "phase": ")" + phase + R"(", "match_nodes": ["a"], "retypes": ["0:b"]}])";
}

}

TEST_CASE("the shipped ruleset loads and is about thirty rules", "[quest]")
{
    REQUIRE(shippedRules().error.empty());
    const std::size_t total = shippedRules().tension.rules.size() + shippedRules().structure.rules.size() + shippedRules().detail.rules.size();
    CHECK(total >= 28);
    CHECK(total <= 40);
    CHECK(shippedRules().tension.rules.size() >= 6);
}

TEST_CASE("every rule can fire: its placeholder is produced by an earlier phase", "[quest]")
{
    std::set<std::string> produced(std::begin(kTensionKinds), std::end(kTensionKinds));
    for (const RuleSet* phase : {&shippedRules().tension, &shippedRules().structure, &shippedRules().detail}) {
        std::set<std::string> next = produced;
        for (const Rule& rule : phase->rules) {
            INFO(rule.name);
            for (const NodePattern& match : rule.matchNodes) {
                CHECK(produced.contains(match.type));
            }
            for (const std::string& type : rule.addNodes) {
                next.insert(type);
            }
            for (const Retype& retype : rule.retypes) {
                next.insert(retype.type);
            }
        }
        produced = next;
    }
}

TEST_CASE("every tension expands to a valid quest for many seeds", "[quest]")
{
    for (const char* tension : kTensionKinds) {
        for (std::uint64_t seed = 0; seed < 200; ++seed) {
            INFO(tension << " seed " << seed);
            const Graph g = expanded(tension, seed);
            CHECK(validateQuest(g).empty());
            CHECK(countType(g, kSituation) == 1);
            CHECK(countType(g, kWalkAway) == 1);
        }
    }
}

TEST_CASE("expansion is deterministic and the seed changes the shape", "[quest]")
{
    for (const char* tension : kTensionKinds) {
        std::set<std::uint64_t> shapes;
        for (std::uint64_t seed = 0; seed < 100; ++seed) {
            const Graph a = expanded(tension, seed);
            const Graph b = expanded(tension, seed);
            REQUIRE(a.digest() == b.digest());
            shapes.insert(a.digest());
        }
        INFO(tension);
        CHECK(shapes.size() >= 8);
    }
}

TEST_CASE("quests reach three levels deep", "[quest]")
{
    std::size_t deepest = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const Graph g = expanded("broken_dependency", seed);
        std::map<NodeId, std::size_t> memo;
        const std::size_t d = depth(g, situationOf(g), memo);
        CHECK(d >= 2);
        deepest = std::max(deepest, d);
    }
    CHECK(deepest >= 3);
}

TEST_CASE("the grammar leans toward two claimants over one object", "[quest]")
{
    std::size_t rival = 0;
    std::size_t split = 0;
    for (std::uint64_t seed = 0; seed < 400; ++seed) {
        Graph g;
        g.addNode("contested_cell");
        RewriteOptions options;
        options.seed = seed;
        REQUIRE(rewrite(g, shippedRules().tension, options).reason == StopReason::FixedPoint);
        rival += countType(g, "deal_with_rival");
        split += countType(g, "choose_side");
    }
    CHECK(rival > 2 * split);
}

TEST_CASE("a spread of beat kinds appears across seeds", "[quest]")
{
    std::set<std::string> beats;
    for (const char* tension : kTensionKinds) {
        for (std::uint64_t seed = 0; seed < 100; ++seed) {
            const Graph g = expanded(tension, seed);
            for (const auto& [id, node] : g.nodes()) {
                beats.insert(node.type);
            }
        }
    }
    CHECK(beats.size() >= 16);
}

TEST_CASE("unknown or empty tensions fail without crashing", "[quest]")
{
    for (const char* bad : {"", "nonsense", "situation", "obtain_object", "CONTESTED_CELL"}) {
        Params params;
        params.tension = bad;
        const Result result = expand(shippedRules(), params);
        CHECK_FALSE(result.error.empty());
        CHECK(result.graph.nodes().empty());
    }
}

TEST_CASE("hostile rule documents are rejected, never thrown", "[quest]")
{
    for (const char* bad : {"", "{", "null", "{}", "[1]", "[{}]", "[[]]", "\xff\xfe\x00garbage", "[{\"id\": 5, \"phase\": \"tension\"}]"}) {
        CHECK_FALSE(parseRuleset(bad).error.empty());
    }
    CHECK_FALSE(parseRuleset("[]").error.empty());
    CHECK_FALSE(parseRuleset(oneRule("grow")).error.empty());
    CHECK_FALSE(parseRuleset(oneRule("tension")).error.empty()); // other phases empty
    CHECK_FALSE(loadRuleset("no/such/file.json").error.empty());

    const std::string dup = "[" + oneRule("tension", "x").substr(1, oneRule("tension", "x").size() - 2) + ","
        + oneRule("structure", "x").substr(1, oneRule("structure", "x").size() - 2) + "]";
    CHECK(parseRuleset(dup).error.find("duplicate") != std::string::npos);
}

TEST_CASE("a ruleset that never settles errors instead of looping", "[quest]")
{
    const char* json = R"([
      {"id": "t", "phase": "tension", "match_nodes": ["contested_cell"], "retypes": ["0:grow"]},
      {"id": "s", "phase": "structure", "match_nodes": ["grow"], "add_nodes": ["grow"], "add_edges": ["0>1:then"]},
      {"id": "d", "phase": "detail", "match_nodes": ["zzz"], "retypes": ["0:fetch"]}
    ])";
    const Ruleset rules = parseRuleset(json);
    REQUIRE(rules.error.empty());
    Params params;
    params.tension = "contested_cell";
    CHECK_FALSE(expand(rules, params).error.empty());
}

TEST_CASE("validateQuest names each broken invariant", "[quest]")
{
    Graph noRoot;
    noRoot.addNode("fetch");
    CHECK(hasError(validateQuest(noRoot), "expected one situation"));

    Graph g;
    const NodeId root = g.addNode(kSituation);
    const NodeId req = g.addNode("fetch");
    g.addEdge(root, req, kNeeds);
    CHECK(hasError(validateQuest(g), "no way to refuse"));

    const NodeId out = g.addNode(kWalkAway);
    g.addEdge(root, out, kRefuse);
    CHECK(validateQuest(g).empty());

    g.addNode("fetch");
    CHECK(hasError(validateQuest(g), "unreachable"));

    Graph holes = g;
    holes.addNode("obtain_object");
    CHECK(hasError(validateQuest(holes), "placeholder"));

    Graph noReq;
    const NodeId r2 = noReq.addNode(kSituation);
    noReq.addEdge(r2, noReq.addNode(kWalkAway), kRefuse);
    CHECK(hasError(validateQuest(noReq), "no requirement"));

    Graph twoRoots = g;
    twoRoots.addNode(kSituation);
    CHECK(hasError(validateQuest(twoRoots), "expected one situation"));
}

TEST_CASE("a rule that fans out into many placeholders still settles", "[quest]")
{
    const char* json = R"([
      {"id": "t", "phase": "tension", "match_nodes": ["contested_cell"], "retypes": ["0:situation"],
       "add_nodes": ["p", "p", "p", "p", "p", "walk_away"],
       "add_edges": ["0>1:needs", "0>2:needs", "0>3:needs", "0>4:needs", "0>5:needs", "0>6:refuse"]},
      {"id": "s", "phase": "structure", "match_nodes": ["p"], "retypes": ["0:q"],
       "add_nodes": ["q", "q"], "add_edges": ["0>1:then", "0>2:then"]},
      {"id": "d", "phase": "detail", "match_nodes": ["q"], "retypes": ["0:fetch"]}
    ])";
    const Ruleset rules = parseRuleset(json);
    REQUIRE(rules.error.empty());
    Params params;
    params.tension = "contested_cell";
    const Result result = expand(rules, params);
    CHECK(result.error.empty());
    CHECK(validateQuest(result.graph).empty());
    CHECK(countType(result.graph, "fetch") == 15);
}
