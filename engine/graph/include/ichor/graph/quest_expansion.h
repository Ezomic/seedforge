#pragma once

#include "ichor/graph/graph.h"
#include "ichor/graph/rewrite.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::graph::quest {

// The seed node is one of the tension kinds from docs/04-quests.md. Expansion
// runs three levels: tension -> structure -> detail. Each level's rules only
// match the previous level's placeholders, so every level ends at a fixed point.
inline constexpr const char* kPhaseTension = "tension";
inline constexpr const char* kPhaseStructure = "structure";
inline constexpr const char* kPhaseDetail = "detail";

inline constexpr const char* kTensionKinds[] = {
    "contested_cell", "broken_dependency", "hostility_pair", "pressure_front", "custodian_secret", "player_consequence",
};

// Edge types. "needs" runs from the situation to a requirement that must be
// met, "then" orders beats inside one requirement, "refuse" leads to the walk_away option.
inline constexpr const char* kNeeds = "needs";
inline constexpr const char* kThen = "then";
inline constexpr const char* kRefuse = "refuse";

inline constexpr const char* kSituation = "situation";
inline constexpr const char* kWalkAway = "walk_away";

struct Ruleset {
    RuleSet tension;
    RuleSet structure;
    RuleSet detail;
    std::string error; // empty when the rules loaded
};

// Records use the mission rule format (see mission::parseRuleRecord); "phase"
// is tension, structure or detail. Never throws; a bad document yields a Ruleset with `error` set.
Ruleset parseRuleset(std::string_view json);
Ruleset loadRuleset(const std::filesystem::path& file);

struct Params {
    std::uint64_t seed = 0;
    std::string tension; // one of kTensionKinds
};

struct Result {
    Graph graph;
    std::string error; // empty on success
};

// Fails (error set) on an unknown tension kind or a rule that does not settle.
Result expand(const Ruleset& rules, const Params& params);

// True for the situation and every finished beat, false for placeholders.
bool isConcrete(std::string_view type);

// Structural invariants of an expanded quest; empty when all hold: one
// situation, no placeholder left, every node reachable from the situation,
// at least one requirement and one walk_away reachable by a refuse edge.
std::vector<std::string> validateQuest(const Graph& graph);

}
