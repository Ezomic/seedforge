#pragma once

#include <glm/vec3.hpp>

namespace ichor::stage0 {

// How fast a full look input (lookX of 1) turns the player, before the combat state's tracking cap applies.
constexpr float kLookRateDegPerSec = 240.0f;

// An angle wrapped into [-pi, pi]. A non-finite angle becomes 0.
float wrapAngle(float angle);

// The look axis as the sim may use it: a non-finite value is no input, anything else is clamped to [-1, 1].
float sanitisedLook(float look);

// Turns a world-axes movement vector (x right, -z forward, as the engine builds body intent) so that "forward"
// points along `cameraYaw` (radians, 0 looks down -Z, 90 degrees looks down +X). Length and height are kept.
// A non-finite vector or yaw yields zero movement.
glm::vec3 cameraRelativeIntent(glm::vec3 worldAxesIntent, float cameraYaw);

// The yaw the view looks along: the player's own facing under free look, the direction to the foe under lock-on.
float viewYaw(bool lockOn, float playerYaw, float yawToFoe);

struct FacingStep {
    float yaw = 0.0f;
    bool lockOn = false;
    float look = 0.0f;          // raw look axis; sanitised here
    float yawToFoe = 0.0f;
    float maxTurnDegPerSec = 0.0f; // the combat state's tracking cap, which holds for both modes
    float dt = 0.0f;
};

// The player's facing after one tick. Lock-on turns toward the foe and free look turns by the look axis; both
// are bounded by the tracking cap, so the clamp that replaces i-frames applies to the player in either mode.
float nextPlayerYaw(const FacingStep& step);

}
