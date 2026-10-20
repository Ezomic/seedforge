#pragma once

#include "ichor/core/dmath.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

// The ichor app walks the player relative to a view yaw (0 faces -Z, a positive yaw turns toward +X), and the
// camera looks along the same yaw, so W always walks where the camera looks. Aiming the camera at a fixed point
// instead turned it around once the player walked past that point, and W then walked away from the view.
inline constexpr float kWalkCameraPitch = 0.28f; // radians below the horizon
inline constexpr float kWalkLookDegPerSec = 240.0f; // the turn at a full look axis, as in Stage 0

inline glm::vec3 walkCameraForward(float yaw = 0.0f)
{
    return glm::vec3(std::sin(yaw) * std::cos(kWalkCameraPitch), -std::sin(kWalkCameraPitch), -std::cos(yaw) * std::cos(kWalkCameraPitch));
}

inline glm::quat walkCameraRotation(float yaw = 0.0f)
{
    return glm::quatLookAt(glm::normalize(walkCameraForward(yaw)), glm::vec3(0.0f, 1.0f, 0.0f));
}

// The yaw after one tick of the look axis. Anything that is not a number turns nothing.
inline float nextWalkYaw(float yaw, float look, double dt)
{
    constexpr float kPi = 3.14159265358979323846f;
    if (!std::isfinite(yaw) || !std::isfinite(look) || !std::isfinite(dt) || dt < 0.0) return std::isfinite(yaw) ? yaw : 0.0f;
    const float turned = yaw + std::clamp(look, -1.0f, 1.0f) * kWalkLookDegPerSec * (kPi / 180.0f) * static_cast<float>(dt);
    float wrapped = turned;
    while (wrapped > kPi) wrapped -= 2.0f * kPi;
    while (wrapped < -kPi) wrapped += 2.0f * kPi;
    return wrapped;
}

// Turns a walking velocity (forward is -Z) so that forward points along the view yaw. A zero yaw changes nothing, bit for bit.
inline glm::vec3 walkRelativeIntent(glm::vec3 intent, float yaw)
{
    if (yaw == 0.0f) return intent;
    const float s = dmath::sin(yaw);
    const float c = dmath::cos(yaw);
    const float right = intent.x;
    const float ahead = -intent.z;
    return {right * c + ahead * s, intent.y, right * s - ahead * c};
}

}
