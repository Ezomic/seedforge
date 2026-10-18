#include "ichor/ledger/quest_coverage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace ichor;
using namespace ichor::questcoverage;
using ichor::graph::quest::Ruleset;

namespace {

const Ruleset& shippedRules()
{
    static const Ruleset rules = graph::quest::loadRuleset(std::string(ICHOR_REPO_DATA) + "/quests/expansion_rules.json");
    return rules;
}

const CoverageReport& shippedReport()
{
    static const CoverageReport report = run(shippedRules(), Options{});
    return report;
}

bool listed(const std::vector<std::string>& names, const std::string& id)
{
    return std::find(names.begin(), names.end(), id) != names.end();
}

// One rule per phase for the given tension, plus whatever extra records the case adds.
Ruleset rulesFrom(const std::string& json)
{
    Ruleset rules = graph::quest::parseRuleset(json);
    REQUIRE(rules.error.empty());
    return rules;
}

const char* kCleanRules = R"([
  {"id": "t_contested", "phase": "tension", "match_nodes": ["contested_cell"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "t_dependency", "phase": "tension", "match_nodes": ["broken_dependency"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "t_hostility", "phase": "tension", "match_nodes": ["hostility_pair"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "t_pressure", "phase": "tension", "match_nodes": ["pressure_front"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "t_secret", "phase": "tension", "match_nodes": ["custodian_secret"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "t_consequence", "phase": "tension", "match_nodes": ["player_consequence"], "retypes": ["0:situation"],
   "add_nodes": ["obtain_object", "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"]},
  {"id": "s_trade", "phase": "structure", "match_nodes": ["obtain_object"], "retypes": ["0:trade"],
   "add_nodes": ["deliver"], "add_edges": ["0>1:then"]},
  {"id": "d_unused", "phase": "detail", "match_nodes": ["find_it"], "retypes": ["0:scout"]}
])";

} // namespace

TEST_CASE("every shipped expansion rule fires somewhere in a 256 seed batch", "[questcoverage]")
{
    const CoverageReport& report = shippedReport();
    INFO(formatTable(report));
    CHECK(report.seeds == 256);
    CHECK(report.quests == 256 * 6);
    CHECK(report.deadRules.empty());
    for (const RuleCoverage& rule : report.rules) {
        CHECK(rule.fires > 0);
        CHECK(rule.quests > 0);
        CHECK(rule.quests <= rule.fires);
    }
}

// Known grammar gap, not a counting artefact: some expansions (for example
// pressure_front through protect_evacuate and truth_investigate) have no beat that
// faces another party, so the player has nobody to tell. Fixing the rules makes
// this go to zero; update the constant then.
constexpr std::uint64_t kKnownNoOtherSideQuests = 72;

TEST_CASE("the shipped batch expands, replays and keeps its structure and visible number promises", "[questcoverage]")
{
    const CoverageReport& report = shippedReport();
    INFO(formatTable(report));
    CHECK(report.expandFailures == 0);
    CHECK(report.replayMismatches == 0);
    CHECK(report.structureFailures == 0);
    CHECK(report.visibleFailures() == 0);
    CHECK(report.visibleMoves == report.quests);
}

TEST_CASE("the only refusal gap in the shipped batch is the known missing other side", "[questcoverage]")
{
    const CoverageReport& report = shippedReport();
    INFO(formatTable(report));
    CHECK(report.refusalFailures == report.refusalNoOtherSide);
    CHECK(report.refusalNoOtherSide == kKnownNoOtherSideQuests);
}

TEST_CASE("the report covers every rule in the file, in file order by phase", "[questcoverage]")
{
    const CoverageReport& report = shippedReport();
    const Ruleset& rules = shippedRules();
    REQUIRE(report.rules.size() == rules.tension.rules.size() + rules.structure.rules.size() + rules.detail.rules.size());
    CHECK(report.rules.front().id == rules.tension.rules.front().name);
    CHECK(report.rules.front().phase == "tension");
    CHECK(report.rules.back().id == rules.detail.rules.back().name);
    CHECK(report.rules.back().phase == "detail");
}

TEST_CASE("a rule that can never match is reported dead and fails the batch", "[questcoverage]")
{
    const CoverageReport report = run(rulesFrom(kCleanRules), Options{});
    INFO(formatTable(report));
    REQUIRE(report.deadRules.size() == 1);
    CHECK(report.deadRules.front() == "d_unused");
    CHECK(!report.ok());
    CHECK(formatTable(report).find("DEAD") != std::string::npos);
    const auto fired = std::find_if(report.rules.begin(), report.rules.end(), [](const RuleCoverage& r) { return r.id == "s_trade"; });
    REQUIRE(fired != report.rules.end());
    CHECK(fired->fires == report.quests);
}

TEST_CASE("rules competing for one match both fire over a batch", "[questcoverage]")
{
    std::string json = kCleanRules;
    const std::string extra =
        R"(, {"id": "s_scout", "phase": "structure", "weight": 1, "match_nodes": ["obtain_object"], "retypes": ["0:scout"]}])";
    json.replace(json.rfind(']'), 1, extra);
    const CoverageReport report = run(rulesFrom(json), Options{});
    CHECK(!listed(report.deadRules, "s_scout"));
    CHECK(listed(report.deadRules, "d_unused"));
}

TEST_CASE("a ruleset with no walk_away is a refusal and structure failure on every quest", "[questcoverage]")
{
    std::string json = kCleanRules;
    for (std::size_t at; (at = json.find(R"(, "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"])")) != std::string::npos;) {
        json.replace(at, std::string(R"(, "walk_away"], "add_edges": ["0>1:needs", "0>2:refuse"])").size(), R"(], "add_edges": ["0>1:needs"])");
    }
    const CoverageReport report = run(rulesFrom(json), Options{32, 7});
    INFO(formatTable(report));
    CHECK(report.structureFailures == report.quests);
    CHECK(!report.ok());
}

TEST_CASE("quests that move no number are chores, and cancelling effects too", "[questcoverage]")
{
    graph::Graph scouts;
    const auto root = scouts.addNode("situation");
    scouts.addEdge(root, scouts.addNode("scout"), "needs");
    scouts.addEdge(root, scouts.addNode("parley"), "needs");
    CHECK(checkVisibleNumber(effectsOf(scouts, "no_such_tension")).verdict == VisibleNumberVerdict::NoEffects);
    CHECK(checkVisibleNumber(effectsOf(scouts, "contested_cell")).verdict == VisibleNumberVerdict::Moves);

    graph::Graph cancelling;
    const auto at = cancelling.addNode("situation");
    cancelling.addEdge(at, cancelling.addNode("sabotage"), "needs");
    cancelling.addEdge(at, cancelling.addNode("persuade"), "needs");
    CHECK(checkVisibleNumber(effectsOf(cancelling, "no_such_tension")).verdict == VisibleNumberVerdict::AllCancel);
    CHECK(checkVisibleNumber(effectsOf(cancelling, "custodian_secret")).verdict == VisibleNumberVerdict::Moves);
}

TEST_CASE("the refusal view puts the situation first and leaves walk_away out", "[questcoverage]")
{
    graph::Graph g;
    const auto root = g.addNode("situation");
    const auto fetch = g.addNode("fetch");
    const auto parley = g.addNode("parley");
    const auto away = g.addNode("walk_away");
    g.addEdge(root, fetch, "needs");
    g.addEdge(fetch, parley, "then");
    g.addEdge(root, away, "refuse");

    const auto quest = refusalViewOf(g);
    REQUIRE(quest.steps.size() == 3);
    CHECK(quest.start == root);
    CHECK(quest.steps[0].id == root);
    CHECK(quest.steps[0].otherSide == 0);
    CHECK(quest.steps[1].otherSide == 0);
    CHECK(quest.steps[2].id == parley);
    CHECK(quest.steps[2].otherSide == root);
    CHECK(graph::questrefusal::ensureRefusals(quest).findings.empty());

    CHECK(refusalViewOf(graph::Graph{}).steps.empty());
}

TEST_CASE("a quest with no party facing beat cannot tell the other side", "[questcoverage]")
{
    graph::Graph g;
    const auto root = g.addNode("situation");
    g.addEdge(root, g.addNode("fetch"), "needs");
    CHECK(!graph::questrefusal::ensureRefusals(refusalViewOf(g)).findings.empty());
}

TEST_CASE("coverage is deterministic and bounded", "[questcoverage]")
{
    const std::string a = formatTable(run(shippedRules(), Options{24, 99}));
    const std::string b = formatTable(run(shippedRules(), Options{24, 99}));
    CHECK(a == b);
    CHECK(run(shippedRules(), Options{0, 1}).seeds == 1);
    CHECK(run(shippedRules(), Options{1u << 30, 1}).seeds == kMaxSeeds);
}

TEST_CASE("a broken ruleset reports an error instead of throwing", "[questcoverage]")
{
    const CoverageReport report = run(graph::quest::parseRuleset("not json"), Options{});
    CHECK(report.expandFailures == 1);
    CHECK(!report.ok());
    CHECK(!report.failureSamples.empty());
}
