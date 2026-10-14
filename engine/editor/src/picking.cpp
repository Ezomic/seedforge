#include "ichor/editor/picking.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ichor {

namespace {

constexpr double kMinScale = 1e-6;
constexpr double kParallel = 1e-12;

bool finite(glm::dvec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Where the ray first enters the box, or zero when it starts inside; nothing when it misses.
std::optional<double> slab(glm::dvec3 origin, glm::dvec3 direction, glm::dvec3 low, glm::dvec3 high)
{
    double near = 0.0;
    double far = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < kParallel) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        double entry = (low[axis] - origin[axis]) / direction[axis];
        double exit = (high[axis] - origin[axis]) / direction[axis];
        if (entry > exit) {
            std::swap(entry, exit);
        }
        near = std::max(near, entry);
        far = std::min(far, exit);
        if (near > far) {
            return std::nullopt;
        }
    }
    return near;
}

}

std::optional<PickRay> pickRay(glm::dvec2 pixel, glm::dvec2 viewport, const PickCamera& camera)
{
    const bool numbers = std::isfinite(pixel.x) && std::isfinite(pixel.y) && std::isfinite(viewport.x) && std::isfinite(viewport.y)
        && finite(camera.position) && std::isfinite(camera.fovY);
    if (!numbers || viewport.x <= 0.0 || viewport.y <= 0.0 || pixel.x < 0.0 || pixel.y < 0.0 || pixel.x > viewport.x || pixel.y > viewport.y) {
        return std::nullopt;
    }
    if (camera.fovY <= 0.0f || camera.fovY >= 3.1415f) {
        return std::nullopt;
    }
    const glm::dquat rotation(camera.rotation);
    const double length = glm::length(rotation);
    if (!std::isfinite(length) || length < 1e-6) {
        return std::nullopt;
    }
    const double reach = std::tan(static_cast<double>(camera.fovY) * 0.5);
    const double aspect = viewport.x / viewport.y;
    const glm::dvec3 local((pixel.x / viewport.x * 2.0 - 1.0) * aspect * reach, (1.0 - pixel.y / viewport.y * 2.0) * reach, -1.0);
    return PickRay{camera.position, glm::normalize((rotation / length) * local)};
}

std::optional<StableId> pickEntity(const Scene& scene, const PickRay& ray, const std::vector<PickBounds>& meshes)
{
    if (!finite(ray.origin) || !finite(ray.direction) || glm::length(ray.direction) < kParallel) {
        return std::nullopt;
    }
    std::optional<StableId> best;
    double bestDistance = 0.0;
    for (auto [entity, id, transform] : scene.registry().view<const StableId, const Transform>().each()) {
        glm::dvec3 low;
        glm::dvec3 high;
        const auto* mesh = scene.registry().try_get<MeshRef>(entity);
        const auto* box = scene.registry().try_get<StaticBox>(entity);
        if (mesh != nullptr && mesh->mesh < meshes.size()) {
            low = meshes[mesh->mesh].min;
            high = meshes[mesh->mesh].max;
        } else if (box != nullptr) {
            low = -glm::dvec3(box->halfExtents);
            high = glm::dvec3(box->halfExtents);
        } else {
            continue;
        }
        if (low.x > high.x || low.y > high.y || low.z > high.z) {
            continue; // a mesh without vertices keeps an inverted box, which the slab test would read as the whole world
        }
        const glm::dvec3 scale(transform.scale);
        if (!finite(transform.position) || !finite(scale) || std::abs(scale.x) < kMinScale || std::abs(scale.y) < kMinScale || std::abs(scale.z) < kMinScale) {
            continue;
        }
        const glm::dquat unturn = glm::inverse(glm::dquat(transform.rotation));
        const glm::dvec3 origin = (unturn * (ray.origin - transform.position)) / scale;
        const glm::dvec3 direction = (unturn * ray.direction) / scale;
        if (!finite(origin) || !finite(direction)) {
            continue;
        }
        const auto distance = slab(origin, direction, low, high);
        if (distance && (!best || *distance < bestDistance || (*distance == bestDistance && id < *best))) {
            best = id;
            bestDistance = *distance;
        }
    }
    return best;
}

}
