#include "ichor/graph/quest_expansion.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace ichor::graph;
using namespace ichor::graph::quest;

namespace {

const Ruleset& shippedRules()
{
    static const Ruleset rules = loadRuleset(std::string(ICHOR_REPO_DATA) + "/quests/expansion_rules.json");
    return rules;
}

bool has(const Graph& g, const char* type)
{
    return std::any_of(g.nodes().begin(), g.nodes().end(), [&](const auto& n) { return n.second.type == type; });
}

// Mean over every tension kind of the share of seeds whose tension step yields
// the two-claimants shape: an object to obtain and a rival claimant to deal with.
double claimantShare(const RuleSet& tension)
{
    constexpr std::uint64_t kSeeds = 300;
    double total = 0.0;
    for (const char* kind : kTensionKinds) {
        std::size_t hits = 0;
        for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
            Graph g;
            g.addNode(kind);
            RewriteOptions options;
            options.seed = seed;
            REQUIRE(rewrite(g, tension, options).reason == StopReason::FixedPoint);
            if (has(g, "obtain_object") && has(g, "deal_with_rival")) {
                ++hits;
            }
        }
        total += static_cast<double>(hits) / static_cast<double>(kSeeds);
    }
    return total / static_cast<double>(std::size(kTensionKinds));
}

}

TEST_CASE("the claimant rules shift the average quest mix toward two claimants one object", "[quest]")
{
    REQUIRE(shippedRules().error.empty());
    const RuleSet& biased = shippedRules().tension;

    RuleSet unbiased;
    for (const Rule& rule : biased.rules) {
        if (rule.name.rfind("claimants_", 0) != 0) {
            unbiased.rules.push_back(rule);
        }
    }
    REQUIRE(unbiased.rules.size() < biased.rules.size());

    const double before = claimantShare(unbiased);
    const double after = claimantShare(biased);
    CHECK(before < 0.4);
    CHECK(after > 0.5);
    CHECK(after > before + 0.2);
}

TEST_CASE("every tension kind can reach the two-claimants shape", "[quest]")
{
    for (const char* kind : kTensionKinds) {
        INFO(kind);
        bool reached = false;
        for (std::uint64_t seed = 0; seed < 200 && !reached; ++seed) {
            Graph g;
            g.addNode(kind);
            RewriteOptions options;
            options.seed = seed;
            REQUIRE(rewrite(g, shippedRules().tension, options).reason == StopReason::FixedPoint);
            reached = has(g, "obtain_object") && has(g, "deal_with_rival");
        }
        CHECK(reached);
    }
}
