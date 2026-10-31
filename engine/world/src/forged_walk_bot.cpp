#include "ichor/world/forged_walk_bot.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

double flatDistance(const glm::dvec3& a, const glm::dvec3& b)
{
    const double dx = a.x - b.x;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool finite(const glm::dvec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}

WalkResult walkTo(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, const std::vector<glm::dvec3>& path, const WalkBotParams& params)
{
    WalkResult result;
    result.end = from;
    if (!finite(from) || !finite(goal) || !(params.dt > 0.0) || !(params.metresPerSecond > 0.0)) {
        result.outcome = WalkOutcome::Blocked;
        result.blocker = WalkBlock::OffMap;
        result.report = "the walk could not start: the start, goal or pace is not a number";
        return result;
    }

    std::vector<glm::dvec3> waypoints;
    for (const glm::dvec3& p : path) {
        if (finite(p)) waypoints.push_back(p);
    }
    waypoints.push_back(goal);

    StuckDetector detector(params.stuck);
    detector.reset(from);
    glm::dvec3 position = from;
    std::size_t next = 0;
    const double probe = walking.params().probeMetres;

    while (result.seconds < params.maxSeconds) {
        if (flatDistance(position, goal) <= params.arriveMetres) {
            result.outcome = WalkOutcome::Arrived;
            break;
        }
        while (next + 1 < waypoints.size() && flatDistance(position, waypoints[next]) <= params.waypointMetres) {
            ++next;
        }
        const glm::dvec3& target = waypoints[next];
        const double away = flatDistance(position, target);
        const glm::dvec3 heading = away > 1.0e-9 ? glm::dvec3((target.x - position.x) / away, 0.0, (target.z - position.z) / away) : glm::dvec3(0.0);

        const WalkAssessment here = walking.assess(position.x, position.z);
        const glm::dvec3 wanted = heading * (params.metresPerSecond * (here.walkable() ? here.wadeFactor : walking.crawlFactor()));
        const glm::dvec3 allowed = walking.filterVelocity(position, wanted);
        const WalkAssessment ahead = walking.assess(position.x + heading.x * probe, position.z + heading.z * probe);

        const glm::dvec3 before = position;
        position.x += allowed.x * params.dt;
        position.z += allowed.z * params.dt;
        result.metresWalked += flatDistance(before, position);
        result.seconds += params.dt;
        if (params.onStep) {
            params.onStep(position, result.seconds);
        }

        const StuckReport stuck = detector.step(position, true, walking.assess(position.x, position.z), ahead.block, params.dt);
        if (stuck.stuck) {
            result.outcome = WalkOutcome::Blocked;
            result.blocker = stuck.cause;
            result.report = describeStuck(stuck);
            break;
        }
    }
    result.end = position;
    result.metresLeft = flatDistance(position, goal);
    return result;
}

}
