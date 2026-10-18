#include "ichor/stage0/free_look.h"

#include "ichor/core/dmath.h"

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr float kPi = 3.14159265358979323846f;

}

float wrapAngle(float angle)
{
    if (!std::isfinite(angle)) {
        return 0.0f;
    }
    while (angle > kPi) angle -= 2.0f * kPi;
    while (angle < -kPi) angle += 2.0f * kPi;
    return angle;
}

float sanitisedLook(float look)
{
    return std::isfinite(look) ? std::clamp(look, -1.0f, 1.0f) : 0.0f;
}

glm::vec3 cameraRelativeIntent(glm::vec3 worldAxesIntent, float cameraYaw)
{
    if (!std::isfinite(cameraYaw) || !std::isfinite(worldAxesIntent.x) || !std::isfinite(worldAxesIntent.y) || !std::isfinite(worldAxesIntent.z)) {
        return glm::vec3(0.0f);
    }
    const float sinYaw = dmath::sin(cameraYaw);
    const float cosYaw = dmath::cos(cameraYaw);
    const float right = worldAxesIntent.x;
    const float ahead = -worldAxesIntent.z;
    return {right * cosYaw + ahead * sinYaw, worldAxesIntent.y, right * sinYaw - ahead * cosYaw};
}

float viewYaw(bool lockOn, float playerYaw, float yawToFoe)
{
    return lockOn ? yawToFoe : playerYaw;
}

float nextPlayerYaw(const FacingStep& step)
{
    const float yaw = std::isfinite(step.yaw) ? step.yaw : 0.0f;
    const float dt = std::isfinite(step.dt) ? std::max(step.dt, 0.0f) : 0.0f;
    const float cap = std::isfinite(step.maxTurnDegPerSec) ? std::max(step.maxTurnDegPerSec, 0.0f) : 0.0f;
    const float maxStep = cap * (kPi / 180.0f) * dt;
    float wanted = 0.0f;
    if (step.lockOn) {
        wanted = wrapAngle(step.yawToFoe - yaw);
    } else {
        wanted = sanitisedLook(step.look) * kLookRateDegPerSec * (kPi / 180.0f) * dt;
    }
    return wrapAngle(yaw + std::clamp(wanted, -maxStep, maxStep));
}

}
