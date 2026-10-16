#pragma once

#include "ichor/graph/embedding.h"
#include "ichor/graph/graph.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ichor::graph::routing {

using embedding::Cell;

inline constexpr std::uint32_t kMaxMargin = 8;
inline constexpr std::uint64_t kMaxVisits = 2000000;

enum class Step {
    Walk,  // same floor, one cell along x or y
    Stair, // one floor up or down, walkable both ways
    Drop,  // one floor down on a one-way link
    Climb, // one floor up on a one-way link
};

struct Params {
    std::uint32_t maxExtent = 24; // footprint side on each horizontal axis, clamped to [1, embedding::kMaxExtentLimit]
    std::uint32_t maxFloors = 4;  // clamped to [1, embedding::kMaxFloorsLimit]
    std::uint32_t margin = 2;     // free cells around the rooms that corridors may use, clamped to kMaxMargin
};

// One graph edge laid out in cells. `path` runs from the cell of `from` to the
// cell of `to`, both inclusive; steps[i] joins path[i] to path[i + 1].
struct Connection {
    EdgeId edge = 0;
    NodeId from = 0;
    NodeId to = 0;
    std::vector<Cell> path;
    std::vector<Step> steps;
};

struct Result {
    std::vector<Connection> connections; // one per edge, in edge id order
    std::string error;                   // empty on success; connections are empty otherwise
};

// Lays out every edge of `graph` on top of an embedding. "path" edges already
// join touching cells, so they become a single step (a stair when they change
// floor). "unlocks" and "shortcut" edges are routed along free cells by a
// breadth-first search with a fixed direction order, so the result is
// deterministic; each routed cell belongs to exactly one connection and never
// to a room. A shortcut is one way, so its floor changes are drops (down) or
// climbs (up); every other floor change is a stair, and every floor change
// spans exactly one floor. Corridors stay inside the rooms' bounding box grown
// by `margin`, cut down to the params' footprint. Never throws; an invalid
// embedding or an edge with no route yields an error.
Result route(const Graph& graph, const std::map<NodeId, Cell>& cells, const Params& params);

// Empty when `result` is a valid routing of `graph` over `cells`: one
// connection per edge, joined end to end, no cell used twice or shared with a
// room, floor changes of one floor, steps matching their cells, and every room
// reachable from the entry along them (respecting one-way steps).
std::vector<std::string> check(const Graph& graph, const std::map<NodeId, Cell>& cells, const Result& result, const Params& params);

inline constexpr std::uint32_t kMaxLayoutAttempts = 64;

struct Layout3d {
    std::map<NodeId, Cell> cells;
    Result routing;
    std::string error; // empty on success
};

// Embeds the mission and routes its connections. A room can end up with every
// free face taken by corridors, which makes an embedding unroutable, so a
// failed routing retries the embedding with the next seed, up to `attempts`
// (clamped to [1, kMaxLayoutAttempts]). Deterministic for a given graph and
// params.
Layout3d layout(const Graph& graph, const embedding::Params& embedParams, const Params& routeParams, std::uint32_t attempts);

}
