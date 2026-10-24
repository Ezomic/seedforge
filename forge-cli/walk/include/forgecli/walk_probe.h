#pragma once

#include <forgecli/showcase.h>

#include "ichor/world/forged_walk_route.h"
#include "ichor/world/forged_walking.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace forgecli {

// The engine side of the showcase picker (SEED-650): where the player would spawn in a forged world, whether
// that spawn is a pocket, and whether the walk bot gets from it to the nearest other settlement. Headless: it
// builds the heightfield, water and walking rules the game builds and never opens a window or touches a GPU.
struct WalkProbeParams {
    double latticeMetres = 8.0;       // the reach search steps over this lattice
    double pocketReachMetres = 100.0; // a spawn from which the walker cannot get this far on foot is a pocket
    double pocketCapMetres = 160.0;   // the reach search stops here
    double minTripMetres = 60.0;      // a settlement closer than this to the spawn is not a trip
    double arriveMetres = 12.0;       // the bot has arrived this close to the settlement's walkable point
    double goalSearchMetres = 120.0;  // how far from a settlement's centre its walkable point is searched
    ichor::WalkRouteParams route;     // a coarser stepMetres makes the route search cheaper (the PR tests use that)
};

// How far from (x, z) a walker can get on foot, straight-line metres, up to capMetres. A start on ground
// the rules refuse still counts as a start (the walker walks out); a start off the map reaches 0.
double walkReachMetres(const ichor::ForgedWalking& walking, double x, double z, double capMetres, double latticeMetres = 8.0);

// The engine's ground route (findWalkRoute, SEED-555) as plain waypoints between the two points. False when there
// is none or an input is not a number.
bool routeOnFoot(const ichor::ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& to, std::vector<glm::dvec3>& path, const ichor::WalkRouteParams& params = {});

WalkProbeResult probeWalk(const worldforge::ForgedWorld& world, const WalkProbeParams& params = {});

WalkProbe makeWalkProbe(const WalkProbeParams& params = {});

}
