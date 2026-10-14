#pragma once

#include "ichor/sim/scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <vector>

namespace ichor {

// The camera as picking needs it: the pose and field of view the renderer uses.
struct PickCamera {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // looks down -Z, Y up
    float fovY = 1.0471976f;
};

struct PickRay {
    glm::dvec3 origin{0.0};
    glm::dvec3 direction{0.0, 0.0, -1.0}; // unit length
};

// Local-space box of one mesh, indexed by MeshRef::mesh.
struct PickBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// The ray from the camera through a pixel (origin top left) of a viewport of the given
// size in pixels. Nothing for a pixel outside the viewport, a viewport without area, or
// values that are not numbers.
std::optional<PickRay> pickRay(glm::dvec2 pixel, glm::dvec2 viewport, const PickCamera& camera);

// The nearest entity whose box the ray crosses. An entity is its mesh's bounds when
// `meshes` knows the mesh, else its static box; anything with neither cannot be seen and
// is skipped. A ray that starts inside a box hits it at distance zero, and equal
// distances go to the lowest id, so the answer does not depend on registry order.
std::optional<StableId> pickEntity(const Scene& scene, const PickRay& ray, const std::vector<PickBounds>& meshes);

}
