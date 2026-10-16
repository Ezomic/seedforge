#pragma once

#include "ichor/graph/dungeon_validate_layout.h"
#include "ichor/graph/graph.h"

#include <cstdint>
#include <map>
#include <vector>
#include <string>

namespace ichor::graph::embedding {

struct Cell {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t floor = 0;

    friend bool operator==(const Cell&, const Cell&) = default;
    friend auto operator<=>(const Cell&, const Cell&) = default;
};

inline constexpr std::uint32_t kMaxExtentLimit = 64;
inline constexpr std::uint32_t kMaxFloorsLimit = 16;
inline constexpr std::uint64_t kMaxSteps = 200000;

struct Params {
    std::uint64_t seed = 0;
    std::uint32_t maxExtent = 24; // footprint side in cells on each horizontal axis, clamped to [1, kMaxExtentLimit]
    std::uint32_t maxFloors = 4;  // distinct floors available, clamped to [1, kMaxFloorsLimit]
};

struct Result {
    std::map<NodeId, Cell> cells; // one distinct cell per room; the entry sits at 0,0,0
    dungeon::Layout layout;       // the validated room and link description
    std::string error;            // empty on success; cells and layout are empty otherwise
};

// Places every room of a mission graph on an integer grid. Rooms take distinct
// cells, every "path" edge joins two cells that touch (one step along x or y on
// the same floor, or one floor up or down at the same x and y, as a stair or
// drop would), and the whole footprint fits in maxExtent x maxExtent x
// maxFloors. "unlocks" and "shortcut" edges are logical links and may span any
// distance: routing them is left to the 3D layout step. The graph must pass
// dungeon::validateLayout and every room must be joined to the entry by path
// edges. Deterministic for a given graph and params (seed picks among the
// feasible placements). Never throws; a graph that is rejected, or that has no
// placement within the footprint and a fixed search budget, yields an error.
Result embed(const Graph& graph, const Params& params);

// Empty when `cells` is a valid embedding of `graph` within the params'
// footprint; otherwise one message per problem found.
std::vector<std::string> check(const Graph& graph, const std::map<NodeId, Cell>& cells, const Params& params);

}
