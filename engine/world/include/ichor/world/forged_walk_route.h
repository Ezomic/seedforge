#pragma once

#include "ichor/world/forged_walking.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// A route over the forged ground that the walking rules accept (SEED-555). The straight line from a spawn to a
// settlement crosses cliffs and deep water the walker is stopped by; the navmesh is built stricter than the walker
// (45 degrees against 50, fords of half a metre against a chest-deep limit) and may not cover the whole trip. This
// searches the ground the walker itself assesses, so a route it finds is one the walker can follow, and a failure
// names why there is none.
struct WalkRouteParams {
    double stepMetres = 4.0;          // grid spacing of the search; widened on its own when the window would be too big
    double checkMetres = 1.0;         // spacing of the ground probes along a move, the walker's own probe length
    double clearanceMetres = 1.0;     // a shortcut must also keep this much walkable ground to either side
    double marginMetres = 300.0;      // how far outside the box of the trip the search may go before it tries the whole world
    double goalRadiusMetres = 8.0;    // arriving this near the goal is arriving
    std::size_t maxNodes = 6'000'000; // upper bound on grid nodes, so a huge world cannot allocate without limit
};

enum class WalkRouteFailure : std::uint8_t {
    None,
    BadInput,      // a non-finite point or parameter, or a point off the map
    StartBlocked,  // no walkable ground near the start
    GoalBlocked,   // no walkable ground near the goal
    NoGroundRoute, // both ends stand on walkable ground, but cliffs or deep water cut them apart
};

const char* walkRouteFailureName(WalkRouteFailure failure);

struct WalkRoute {
    bool found = false;
    WalkRouteFailure failure = WalkRouteFailure::BadInput;
    std::vector<glm::dvec3> waypoints; // from the start towards the goal, y left at 0
    double metres = 0.0;               // length of the waypoints
    std::size_t nodesSearched = 0;
    // When NoGroundRoute: the walkable ground the start can reach, and what bounds it (counts of boundary samples).
    double reachableSquareMetres = 0.0;
    std::size_t boundarySteep = 0;
    std::size_t boundaryDeepWater = 0;
    std::size_t boundaryMapEdge = 0;
};

// The ground a failed search showed a start to be shut in by, kept so a retry from the same start towards another goal
// answers at once instead of flooding the same pocket again (SEED-657).
struct WalkRoutePocket {
    double fromX = 0.0;
    double fromZ = 0.0;
    double step = 0.0;
    double goalRadiusMetres = 0.0;
    std::vector<glm::vec2> nodes; // every grid node the start can stand on
    double reachableSquareMetres = 0.0;
    std::size_t boundarySteep = 0;
    std::size_t boundaryDeepWater = 0;
    std::size_t boundaryMapEdge = 0;
};

// Pockets remembered between searches over one ForgedWalking with one set of WalkRouteParams. Pass the same memo to
// every findWalkRoute call that retries starts and goals over the same world.
struct WalkRouteMemo {
    std::vector<WalkRoutePocket> pockets;
};

// "enclosed: 2.1 km2 reachable, bounded 72% by cliffs, 25% by deep water, 3% by the map edge".
std::string describeEnclosure(const WalkRoute& route);

// A start that is shut in by cliffs or deep water is reported at once on the later calls that share `memo`, for any goal
// outside the pocket; a null memo remembers nothing.
WalkRoute findWalkRoute(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, const WalkRouteParams& params = {}, WalkRouteMemo* memo = nullptr);

// How far along the straight line from `from` to `goal` the walker is first stopped, in metres, or a negative
// number when the whole line is walkable. `block` says by what.
double firstBlockOnLine(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, WalkBlock& block);

}
