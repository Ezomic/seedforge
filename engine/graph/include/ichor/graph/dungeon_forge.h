#pragma once

#include "ichor/graph/embedding.h"
#include "ichor/graph/graph.h"
#include "ichor/graph/mission.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ichor::graph::forge {

inline constexpr std::uint32_t kMaxAttempts = 64;

// All that a world keeps of a dungeon (doc 09: forge at generation, instantiate on entry).
// The layout itself is not stored: the same seed and sizes rebuild it, and the room count
// and layout hash catch a ruleset that has changed since the world was forged.
struct Record {
    std::uint64_t seed = 0; // the attempt seed that produced a valid layout
    std::uint8_t locks = 2;
    std::uint8_t growSteps = 6;
    std::uint8_t maxExtent = 24;
    std::uint8_t maxFloors = 4;
    std::uint16_t rooms = 0;
    std::uint64_t layoutHash = 0;

    friend bool operator==(const Record&, const Record&) = default;
};

struct Params {
    std::uint64_t seed = 0; // typically derived from the world seed and the site id
    std::uint32_t locks = 2;
    std::uint32_t growSteps = 6;
    std::uint32_t maxExtent = 24;
    std::uint32_t maxFloors = 4;
};

struct Instance {
    Graph graph;
    std::map<NodeId, embedding::Cell> cells;
    std::vector<NodeId> chambers; // room ids in ascending order: "the third chamber" is chambers[2]
};

struct ForgeResult {
    Record record;
    std::string error; // empty on success
};

struct InstanceResult {
    Instance instance;
    std::string error; // empty on success
};

// Tries up to kMaxAttempts derived seeds until the mission is valid, embeds and passes the
// layout validator. Deterministic. Out-of-range parameters are an error rather than clamped,
// so a record always reproduces itself.
ForgeResult forgeLayout(const mission::Ruleset& rules, const Params& params);

// Rebuilds the dungeon a record describes. Fails (and returns no instance) on an out-of-range
// record or when the rebuilt layout differs from the one that was forged.
InstanceResult instantiate(const mission::Ruleset& rules, const Record& record);

// Stable across platforms: covers rooms, links and cells.
std::uint64_t layoutHash(const Graph& graph, const std::map<NodeId, embedding::Cell>& cells);

// Empty when every field is inside its limits.
std::string validateRecord(const Record& record);

inline constexpr std::size_t kEncodedSize = 4 + 1 + 8 + 4 + 2 + 8 + 8;

std::vector<std::uint8_t> encode(const Record& record);
// Nullopt on a wrong length, magic, version, checksum or any out-of-range field. Never throws.
std::optional<Record> decode(std::span<const std::uint8_t> bytes);

}
