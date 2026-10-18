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

bool inFrustum(const CameraPose& pose, glm::dvec3 centre, float radius, float fovY, float aspect, float nearPlane, float farPlane)
{
    if (!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z) || !std::isfinite(radius)) {
        return false;
    }
    const glm::vec3 view = glm::conjugate(pose.rotation) * glm::vec3(centre - pose.position);
    const float depth = -view.z;
    if (depth + radius < nearPlane || depth - radius > farPlane) {
        return false;
    }
    const float halfV = fovY * 0.5f;
    const float halfH = std::atan(std::tan(halfV) * aspect);
    // Distance of the centre outside each side plane; inside is negative.
    const float outsideX = std::abs(view.x) * std::cos(halfH) - depth * std::sin(halfH);
    const float outsideY = std::abs(view.y) * std::cos(halfV) - depth * std::sin(halfV);
    return outsideX <= radius && outsideY <= radius;
}

}
