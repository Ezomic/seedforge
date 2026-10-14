#include "ichor/editor/nudge.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace ichor {

namespace {

constexpr double kDegenerate = 1e-6;

bool finite(glm::dvec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

glm::dvec3 flat(glm::dvec3 v)
{
    return {v.x, 0.0, v.z};
}

// The ground direction the camera faces, or world -Z when it has none.
glm::dvec3 groundForward(const glm::dquat& rotation)
{
    const glm::dvec3 ahead = rotation * glm::dvec3(0.0, 0.0, -1.0);
    if (glm::length(flat(ahead)) >= kDegenerate) {
        return glm::normalize(flat(ahead));
    }
    // Looking down, the top of the screen points ahead on the ground; looking up, it points behind.
    const glm::dvec3 top = flat(rotation * glm::dvec3(0.0, 1.0, 0.0));
    if (glm::length(top) >= kDegenerate) {
        return glm::normalize(ahead.y < 0.0 ? top : -top);
    }
    return {0.0, 0.0, -1.0};
}

}

std::optional<glm::dvec3> nudgeOffset(const NudgeKeys& keys, const PickCamera& camera)
{
    const glm::dquat raw(camera.rotation);
    const double length = glm::length(raw);
    if (!finite(camera.position) || !std::isfinite(length) || length < kDegenerate) {
        return std::nullopt;
    }
    const int sideways = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
    const int ahead = (keys.forward ? 1 : 0) - (keys.back ? 1 : 0);
    const int lift = (keys.up ? 1 : 0) - (keys.down ? 1 : 0);
    if (sideways == 0 && ahead == 0 && lift == 0) {
        return std::nullopt;
    }
    const double step = keys.fast ? kNudgeFastStep : kNudgeStep;
    const glm::dvec3 forward = groundForward(raw / length);
    const glm::dvec3 right(-forward.z, 0.0, forward.x);
    glm::dvec3 ground = right * static_cast<double>(sideways) + forward * static_cast<double>(ahead);
    if (glm::length(ground) > kDegenerate) {
        ground = glm::normalize(ground);
    }
    return (ground + glm::dvec3(0.0, static_cast<double>(lift), 0.0)) * step;
}

}
