#include "ichor/stage0/camera_rig.h"

#include "ichor/stage0/combat.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

bool finite(glm::dvec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}

glm::dvec3 awayFrom(glm::dvec3 player, glm::dvec3 foe)
{
    glm::dvec3 away = player - foe;
    away.y = 0.0;
    return glm::length(away) > 1e-6 ? glm::normalize(away) : glm::dvec3(0.0, 0.0, 1.0);
}

glm::dvec3 smoothAway(glm::dvec3 current, glm::dvec3 target, double dt, float rate)
{
    if (!finite(current) || glm::length(current) < 1e-6) {
        return target;
    }
    const double step = std::max(dt, 0.0) * std::max(rate, 0.0f);
    const double blend = std::isfinite(step) ? step / (1.0 + step) : 1.0;
    const glm::dvec3 mixed = current * (1.0 - blend) + target * blend;
    return mixed;
}

CameraPose frameCamera(glm::dvec3 player, glm::dvec3 foe, glm::dvec3 away, const CameraTuning& tuning)
{
    if (!finite(player) || !finite(foe) || !finite(away) || !std::isfinite(tuning.distance) || !std::isfinite(tuning.height)
        || !std::isfinite(tuning.wallClearance) || !std::isfinite(tuning.minBoom)) {
        return {};
    }
    // Nothing legitimate is outside the arena; this keeps every product below far from overflow.
    constexpr double kReach = 1000.0;
    player = glm::clamp(player, glm::dvec3(-kReach), glm::dvec3(kReach));
    foe = glm::clamp(foe, glm::dvec3(-kReach), glm::dvec3(kReach));
    const double limit = std::max(0.0, static_cast<double>(kArenaHalf) - std::max(0.0, static_cast<double>(tuning.wallClearance)));
    const bool lockOn = tuning.framing == CameraFraming::LockOn;
    const double separation = glm::length(glm::dvec3(foe.x - player.x, 0.0, foe.z - player.z));
    const double boom = std::max(0.0, static_cast<double>(tuning.distance)) + (lockOn ? 0.15 * separation : 0.0);

    const glm::dvec3 anchor{std::clamp(player.x, -limit, limit), player.y, std::clamp(player.z, -limit, limit)};
    const glm::dvec3 side{-away.z, 0.0, away.x};
    glm::dvec3 offset = away * boom + (lockOn ? side * (0.25 * boom) : glm::dvec3(0.0));

    // Pull the boom in along its own line so the view direction survives, then clamp as the last guarantee.
    double scale = 1.0;
    const double room[2] = {offset.x > 0.0 ? limit - anchor.x : anchor.x + limit, offset.z > 0.0 ? limit - anchor.z : anchor.z + limit};
    const double want[2] = {std::abs(offset.x), std::abs(offset.z)};
    for (int axis = 0; axis < 2; ++axis) {
        if (want[axis] > room[axis]) {
            scale = std::min(scale, room[axis] / want[axis]);
        }
    }
    const double length = glm::length(offset);
    const double roomAhead = length * scale;
    if (length > 1e-9 && roomAhead < tuning.minBoom) {
        scale = std::min(1.0, tuning.minBoom / length);
    }
    offset *= scale;

    glm::dvec3 eye = anchor + offset + glm::dvec3(0.0, tuning.height, 0.0);
    eye.x = std::clamp(eye.x, -limit, limit);
    eye.z = std::clamp(eye.z, -limit, limit);

    // Once a wall leaves less than the follow distance behind the player, a low boom cannot see both fighters,
    // so the camera rises above them and aims at their midpoint. The blend is continuous in the room left.
    const double pinch = tuning.distance > 0.0f ? std::clamp((tuning.distance - roomAhead) / (0.5 * tuning.distance), 0.0, 1.0) : 0.0;
    const glm::dvec3 midpoint = (player + foe) * 0.5;
    if (pinch > 0.0) {
        const double riseHeight = 0.9 + (0.6 * separation + 1.5) / static_cast<double>(kTanHalfFovY);
        glm::dvec3 overhead = midpoint + away * 0.5;
        overhead.x = std::clamp(overhead.x, -limit, limit);
        overhead.z = std::clamp(overhead.z, -limit, limit);
        overhead.y = std::max(riseHeight, static_cast<double>(tuning.height));
        eye = eye + (overhead - eye) * pinch;
    }

    const glm::dvec3 ideal = lockOn ? player * 0.4 + foe * 0.6 : midpoint;
    const glm::dvec3 target = ideal + (midpoint - ideal) * pinch + glm::dvec3(0.0, 0.9, 0.0);
    glm::dvec3 look = target - eye;
    if (glm::length(look) < 1e-6) {
        look = glm::dvec3(0.0, 0.0, -1.0);
    }
    return {eye, glm::quatLookAt(glm::normalize(glm::vec3(look)), glm::vec3(0.0f, 1.0f, 0.0f))};
}

CameraPose CameraRig::update(glm::dvec3 player, glm::dvec3 foe, double dt)
{
    if (!finite(player) || !finite(foe) || !std::isfinite(dt)) {
        return pose_;
    }
    const glm::dvec3 wanted = awayFrom(player, foe);
    away_ = primed_ ? smoothAway(away_, wanted, dt, tuning_.smoothing) : wanted;
    primed_ = true;
    pose_ = frameCamera(player, foe, glm::length(away_) > 1e-3 ? glm::normalize(away_) : wanted, tuning_);
    return pose_;
}

CameraPose followCamera(glm::dvec3 player, glm::dvec3 foe)
{
    return frameCamera(player, foe, awayFrom(player, foe), CameraTuning{});
}

}
