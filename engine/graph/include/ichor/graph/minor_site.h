#pragma once

#include "ichor/graph/graph.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::graph::site {

// Node types of a minor site. Each has a design-time visit cost in seconds,
// summed to estimate a full clear (doc 09: five to ten minutes, length is the
// enemy).
inline constexpr const char* kEntrance = "entrance";
inline constexpr const char* kPassage = "passage";
inline constexpr const char* kRoom = "room";
inline constexpr const char* kEncounter = "encounter";
inline constexpr const char* kLandmark = "landmark";
inline constexpr const char* kStash = "stash";
inline constexpr const char* kReward = "reward";

inline constexpr const char* kPath = "path";
inline constexpr const char* kShortcut = "shortcut";

inline constexpr std::uint32_t kMinSeconds = 300;
inline constexpr std::uint32_t kMaxSeconds = 600;

// Seconds to visit a node of this type; 0 for an unknown type.
std::uint32_t nodeSeconds(std::string_view type);

inline constexpr std::uint32_t kArchetypeCount = 5;
// cave, camp, cellar, wreck, watchtower (each with its own room mix); empty for an out-of-range index.
std::string_view archetypeName(std::uint32_t index);

struct Params {
    std::uint64_t seed = 0;
    // Archetype index, or kArchetypeCount (or more) to let the seed choose.
    std::uint32_t archetype = kArchetypeCount;
};

struct Result {
    Graph graph;
    std::string archetype;
    std::uint32_t seconds = 0; // full-clear estimate, within [kMinSeconds, kMaxSeconds]
};

// Deterministic: the same params give the same graph on every platform.
Result generate(const Params& params);

// Structural invariants of a finished site; empty when all hold: one entrance
// and one reward, only known node and edge types, every node reachable from
// the entrance along path edges, and the full-clear time inside the five to
// ten minute window. Safe on any graph.
std::vector<std::string> validateSite(const Graph& graph);

// Sum of nodeSeconds over every node.
std::uint32_t clearSeconds(const Graph& graph);

}
