#pragma once

#include "ichor/graph/graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph::guidelight {

inline constexpr std::size_t kMaxRooms = 4096;
inline constexpr std::uint32_t kUnreached = 0xFFFFFFFFu;

struct Params {
    NodeId entry = 0;     // 0 = the first node of type "entry"
    NodeId objective = 0; // the room the light should lead to
    bool enabled = true;  // false lights every room the same (the unguided baseline)
    float pathFloor = 0.5f;       // level at the entry, rising to 1 at the objective
    float ambientFloor = 0.3f;    // no room is ever dimmer than this
    float offPathFactor = 0.7f;   // off-path rooms take this share of their anchor's level
    float neutralLevel = 0.6f;    // level of every room when there is no guidance
    float pathWarmthFloor = 0.3f; // warmth at the entry, rising to 1 at the objective
    float ambientWarmth = 0.2f;   // no room is ever cooler than this
    float neutralWarmth = 0.5f;
};

struct RoomLight {
    NodeId room = 0;
    float level = 0.0f;  // 0..1 brightness
    float warmth = 0.0f; // 0 cool .. 1 warm
    // The neighbouring room the light should lead the eye to; 0 for the
    // objective itself and for rooms with no guidance.
    NodeId towards = 0;
    std::uint32_t distance = kUnreached; // hops to the objective over path edges
    bool onCriticalPath = false;
};

struct Result {
    std::vector<RoomLight> rooms;     // ordered by room id; empty on error
    std::vector<NodeId> criticalPath; // entry to objective inclusive; empty when unguided
    bool guided = false;
    std::string error; // empty on success
};

// Assigns every room a light level, warmth and lead-on hint so light gets
// brighter and warmer along the shortest path of "path" edges from the entry to
// the objective (the critical path). Rooms off that path, dead ends included,
// get a share of the level of the critical room they branch from, never fall
// below ambientFloor, and point back towards the objective. Edges are walked in
// both directions; other edge types are ignored. An objective the entry cannot
// reach is not an error: every room gets the neutral light and guided is false.
// Errors: empty graph, more than kMaxRooms rooms, unknown objective or entry,
// no entry, a parameter that is not a finite number in 0..1, or a floor above
// the path floor. Pure and deterministic; ties resolve in a fixed order by id.
// Turning this into light sockets and rendering is deferred.
Result plan(const Graph& graph, const Params& params);

// Mean level of the rooms on (onPath true) or off the critical path; 0 when none.
float meanLevel(const Result& result, bool onPath);

} // namespace ichor::graph::guidelight
