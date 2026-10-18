#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace ichor::stage0 {

// The third-person view of the fight: behind the player, looking over them at the foe.
// main.cpp builds the render camera from this, and the killing-blow telemetry tests
// the attacker against the same pose, so "on screen" means what the player could see.
constexpr float kCameraFovY = 0.7853982f; // 45 degrees, vertical
constexpr float kCameraAspect = 16.0f / 9.0f;
constexpr float kCameraNear = 0.05f;
constexpr float kCameraFar = 100.0f;

struct CameraPose {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // looks down -Z, Y up
};

CameraPose followCamera(glm::dvec3 player, glm::dvec3 foe);

// Whether a sphere of the given radius touches the view frustum. A non-finite input is off screen.
bool inFrustum(const CameraPose& pose, glm::dvec3 centre, float radius, float fovY = kCameraFovY, float aspect = kCameraAspect,
    float nearPlane = kCameraNear, float farPlane = kCameraFar);

}
