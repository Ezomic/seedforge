#include "ichor/stage0/camera_view.h"

#include <glm/geometric.hpp>

#include <cmath>

namespace ichor::stage0 {

CameraPose followCamera(glm::dvec3 player, glm::dvec3 foe)
{
    glm::dvec3 away = player - foe;
    away.y = 0.0;
    away = glm::length(away) > 1e-6 ? glm::normalize(away) : glm::dvec3(0.0, 0.0, 1.0);
    const glm::dvec3 eye = player + away * 3.2 + glm::dvec3(0.0, 2.0, 0.0);
    const glm::dvec3 target = (player + foe) * 0.5 + glm::dvec3(0.0, 0.9, 0.0);
    return {eye, glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f))};
}

bool inFrustum(const CameraPose& pose, glm::dvec3 centre, float radius)
{
    if (!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z) || !std::isfinite(radius)) {
        return false;
    }
    const glm::vec3 view = glm::conjugate(pose.rotation) * glm::vec3(centre - pose.position);
    const float depth = -view.z;
    if (depth + radius < kCameraNear || depth - radius > kCameraFar) {
        return false;
    }
    // A sphere touches a side plane when its centre is within radius / cos(half angle) = radius * sqrt(1 + tan^2) of it.
    const float marginX = radius * std::sqrt(1.0f + kTanHalfFovX * kTanHalfFovX);
    const float marginY = radius * std::sqrt(1.0f + kTanHalfFovY * kTanHalfFovY);
    return std::abs(view.x) <= depth * kTanHalfFovX + marginX && std::abs(view.y) <= depth * kTanHalfFovY + marginY;
}

}
