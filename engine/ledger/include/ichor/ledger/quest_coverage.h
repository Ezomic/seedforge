#pragma once

#include "ichor/graph/graph.h"
#include "ichor/graph/quest_expansion.h"
#include "ichor/graph/quest_refusal.h"
#include "ichor/ledger/quest_visible_effect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::questcoverage {

// Doc 04 grammar coverage: expand every tension kind over a batch of seeds with
// a ruleset and report how often each rule fires, which never fire, and whether
// the refusal and visible-number rules hold across the batch. Pure, integer-only
// and deterministic, so the table is identical on every platform.

inline constexpr std::uint32_t kMaxSeeds = 4096;
inline constexpr std::size_t kMaxFailureSamples = 16;

struct Options {
    std::uint32_t seeds = 256; // clamped to [1, kMaxSeeds]
    std::uint64_t baseSeed = 1;
};

struct RuleCoverage {
    std::string id;
    std::string phase;
    std::uint64_t fires = 0;  // applications over the whole batch
    std::uint64_t quests = 0; // expansions in which it fired at least once
};

struct CoverageReport {
    std::uint32_t seeds = 0;
    std::uint64_t quests = 0;          // expansions attempted: seeds times tension kinds
    std::uint64_t expandFailures = 0;  // expand() returned an error
    std::uint64_t replayMismatches = 0; // the per-rule replay disagreed with expand()
    std::uint64_t structureFailures = 0; // graph::quest::validateQuest reported a problem
    std::uint64_t refusalFailures = 0;   // questrefusal could not be satisfied after ensureRefusals
    std::uint64_t refusalNoOtherSide = 0; // the subset of those with no party to tell
    std::uint64_t visibleMoves = 0;
    std::uint64_t visibleNoEffects = 0;
    std::uint64_t visibleAllCancel = 0;
    std::vector<RuleCoverage> rules; // tension, structure, detail, each in file order
    std::vector<std::string> deadRules;
    std::vector<std::string> failureSamples; // first kMaxFailureSamples failures, in batch order

    std::uint64_t visibleFailures() const { return visibleNoEffects + visibleAllCancel; }
    bool ok() const
    {
        return quests > 0 && expandFailures == 0 && replayMismatches == 0 && structureFailures == 0 && refusalFailures == 0 &&
               visibleFailures() == 0 && deadRules.empty();
    }
};

// What a finished quest declares it changes. The tension moves one number by 8, more than the beats can undo
// (contested_cell and pressure_front the territory, broken_dependency the
// settlement, hostility_pair and custodian_secret a faction, player_consequence
// a route; pressure_front loses it). Then the beats: trade and deliver
// stock the settlement, escort opens a route, evacuate closes one, defend holds
// territory, confront and sabotage weaken a faction, persuade strengthens one,
// heist drains the settlement. Other beats move nothing. Effects on one metric
// share a target, so opposing beats can cancel.
std::vector<QuestEffect> effectsOf(const graph::Graph& graph, const std::string& tension);

// The quest played in order: the situation, then its beats in breadth-first
// node order, each step accepting into the next. Beats that face another party
// (parley, persuade, bribe, warn, sabotage, heist, confront, trade) have that
// party as their other side. walk_away is the refusal, not a step.
graph::questrefusal::Quest refusalViewOf(const graph::Graph& graph);

CoverageReport run(const graph::quest::Ruleset& rules, const Options& options);

// Fixed-width text, one line per rule plus a summary; no floating point.
std::string formatTable(const CoverageReport& report);

} // namespace ichor::questcoverage
