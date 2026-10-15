#pragma once

#include "ichor/graph/graph.h"
#include "ichor/graph/rewrite.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::graph::mission {

// Node types of a finished mission graph. "seg" and "vault" are placeholders
// that only exist while generating.
inline constexpr const char* kEntry = "entry";
inline constexpr const char* kClimax = "climax";
inline constexpr const char* kChallenge = "challenge";
inline constexpr const char* kJunction = "junction";
inline constexpr const char* kGate = "gate";
inline constexpr const char* kKey = "key";
inline constexpr const char* kReward = "reward";
inline constexpr const char* kReveal = "reveal";
inline constexpr const char* kRest = "rest";
inline constexpr const char* kSegment = "seg";
inline constexpr const char* kVault = "vault";

// Edge types. "path" is forward progress, "unlocks" runs from a key to its
// gate, "shortcut" is a one-way link back to earlier space.
inline constexpr const char* kPath = "path";
inline constexpr const char* kUnlocks = "unlocks";
inline constexpr const char* kShortcut = "shortcut";

// Generation runs the phases in this order; each phase has its own rules in
// the data, selected by the record's "phase" field.
inline constexpr const char* kPhaseCore = "core";
inline constexpr const char* kPhaseLock = "lock";
inline constexpr const char* kPhaseGrow = "grow";
inline constexpr const char* kPhaseReveal = "reveal";
inline constexpr const char* kPhaseResolve = "resolve";

struct Ruleset {
    RuleSet core;
    RuleSet lock;
    RuleSet grow;
    RuleSet reveal;
    RuleSet resolve;
    std::string error; // empty when the rules loaded
};

// Rules are data records (a JSON array, each with a string id) so the shared
// loader accepts them. Fields: phase, weight, and the string lists
// match_nodes (types), match_edges ("from>to:type"), delete_nodes ("index"),
// retypes ("index:type"), add_nodes (types), add_edges ("from>to:type").
// Never throws; a bad document yields a Ruleset with `error` set.
Ruleset parseRuleset(std::string_view json);
Ruleset loadRuleset(const std::filesystem::path& file);

struct Params {
    std::uint64_t seed = 0;
    std::uint32_t locks = 2;     // extra lock steps after the guaranteed one, clamped to kMaxLocks
    std::uint32_t growSteps = 6; // clamped to kMaxGrowSteps
};
inline constexpr std::uint32_t kMaxLocks = 16;
inline constexpr std::uint32_t kMaxGrowSteps = 64;

struct Result {
    Graph graph;
    std::string error; // empty on success
};

// entry, a gate with its key and a gated reward, a shortcut back, a reveal
// and the climax are always present; the seed decides everything else.
Result generate(const Ruleset& rules, const Params& params);

// Structural invariants of a finished mission graph; empty when all hold:
// one entry and one climax, no leftover placeholders, every gate has a key,
// every node reachable once keys are collected, no reward or climax reachable
// with every gate shut, every shortcut leads back to a node reachable earlier.
std::vector<std::string> validateMission(const Graph& graph);

}
