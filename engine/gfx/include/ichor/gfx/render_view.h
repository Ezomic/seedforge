#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace ichor {

// What the simulation hands the renderer each frame. World positions are
// doubles; the renderer subtracts the camera before anything touches a float
// (doc 17, architecture rule 8).

struct RenderInstance {
    std::uint32_t mesh = 0;
    glm::dvec3 position{0.0};
    glm::mat3 basis{1.0f}; // rotation and uniform scale; normals are transformed by it directly
};

struct RenderCamera {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // looks down -Z, Y up
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
};

struct DebugLine {
    glm::dvec3 a{0.0};
    glm::dvec3 b{0.0};
    glm::vec3 color{1.0f};
};

struct RenderView {
    RenderCamera camera;
    glm::vec3 sunDirection{0.4f, 0.8f, 0.3f}; // towards the sun
    glm::vec3 sunColor{1.0f, 0.96f, 0.9f};
    float sunIntensity = 3.0f;
    glm::vec3 ambient{0.18f, 0.2f, 0.26f};
    glm::vec3 clearColor{0.45f, 0.58f, 0.75f};
    std::vector<RenderInstance> instances;
    std::vector<DebugLine> lines;
};

}
