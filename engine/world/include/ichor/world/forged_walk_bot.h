#pragma once

#include "ichor/world/forged_walking.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ichor {

// A headless walker: direct kinematic steps over the forged ground under the walking rules, following a
// path (or heading straight when there is none) until it arrives or something stops it. It stands for
// the player in tests and tooling; it never opens a window and needs no physics.
struct WalkBotParams {
    double metresPerSecond = 3.0; // the Player component's default pace
    double dt = 1.0 / 30.0;
    double arriveMetres = 4.0;
    double waypointMetres = 2.0;
    double maxSeconds = 3600.0;
    StuckParams stuck;
    // Called after every step with where the walker stands and the seconds walked so far, so a test or tool can
    // stream the world under the walker tick by tick. Empty by default, which changes nothing about the walk.
    std::function<void(const glm::dvec3& position, double seconds)> onStep;
};

enum class WalkOutcome : std::uint8_t {
    Arrived,
    Blocked,  // stopped, and says by what
    TimedOut, // ran out of time while still moving: not an honest answer, the caller treats it as a failure
};

struct WalkResult {
    WalkOutcome outcome = WalkOutcome::TimedOut;
    WalkBlock blocker = WalkBlock::None;
    glm::dvec3 end{0.0};
    double seconds = 0.0;
    double metresWalked = 0.0;
    double metresLeft = 0.0; // straight line from where it ended to the goal
    std::string report;      // the stuck report, when Blocked
};

// `path` is the waypoints to follow in order (it may be empty), `goal` the point to reach. Non-finite input
// is Blocked at the start with OffMap.
WalkResult walkTo(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, const std::vector<glm::dvec3>& path, const WalkBotParams& params = {});

}
