#include "ichor/stage0/camera_view.h"

#include <glm/geometric.hpp>

#include <cmath>

namespace ichor::stage0 {

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
