#pragma once

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace ichor {

// The ichor app walks the player along world axes (forward is -Z), so the camera must look along them too.
// Aiming it at a fixed point instead turned it around once the player walked past that point, and W then
// walked away from where the camera looked.
inline constexpr float kWalkCameraPitch = 0.28f; // radians below the horizon

inline glm::vec3 walkCameraForward()
{
    return glm::vec3(0.0f, -std::sin(kWalkCameraPitch), -std::cos(kWalkCameraPitch));
}

inline glm::quat walkCameraRotation()
{
    return glm::quatLookAt(walkCameraForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

}
