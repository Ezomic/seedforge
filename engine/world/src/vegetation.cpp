#include "ichor/world/vegetation.h"

#include "ichor/core/dmath.h"
#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kTwoPi = 6.28318530718f;

// A smooth patchwork of forest and clearing: a few overlapping waves of
// distance, so the density has no grid in it. Deterministic through dmath.
float forestDensity(double x, double z, float scale)
{
    const auto fx = static_cast<float>(x / scale);
    const auto fz = static_cast<float>(z / scale);
    const float a = dmath::sin(fx * 1.7f + 0.3f) * dmath::sin(fz * 1.3f + 1.1f);
    const float b = dmath::sin(fx * 3.1f + fz * 2.3f + 2.0f);
    const float value = 0.5f + 0.35f * a + 0.15f * b;
    return std::clamp((value - 0.25f) * 1.8f, 0.0f, 1.0f);
}

struct Builder {
    Mesh mesh;

    std::uint32_t vertex(glm::vec3 position, glm::vec3 normal)
    {
        Vertex v{};
        v.position = position;
        v.normal = glm::normalize(normal);
        v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        v.uv = {0.0f, 0.0f};
        v.weights[0] = 65535;
        mesh.boundsMin = glm::min(mesh.boundsMin, position);
        mesh.boundsMax = glm::max(mesh.boundsMax, position);
        mesh.vertices.push_back(v);
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    }

    // A cone or a cylinder frustum, wound outward, with side normals.
    void frustum(float bottomY, float topY, float bottomRadius, float topRadius, int sides)
    {
        const float slope = (bottomRadius - topRadius) / (topY - bottomY);
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i <= sides; ++i) {
            const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
            const float c = dmath::cos(angle);
            const float s = dmath::sin(angle);
            const glm::vec3 normal(c, slope, s);
            vertex({c * bottomRadius, bottomY, s * bottomRadius}, normal);
            vertex({c * topRadius, topY, s * topRadius}, normal);
        }
        for (int i = 0; i < sides; ++i) {
            const std::uint32_t a = first + static_cast<std::uint32_t>(i) * 2;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
        }
    }

    // A low-poly crown: an octahedron pushed out into a rounded blob.
    void crown(float centreY, float radius)
    {
        const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int faces[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
        for (const auto& face : faces) {
            // Each face is split once so the blob is rounder than a bare octahedron.
            const glm::vec3 p0 = axes[face[0]];
            const glm::vec3 p1 = axes[face[1]];
            const glm::vec3 p2 = axes[face[2]];
            const glm::vec3 m01 = glm::normalize(p0 + p1);
            const glm::vec3 m12 = glm::normalize(p1 + p2);
            const glm::vec3 m20 = glm::normalize(p2 + p0);
            const glm::vec3 tris[4][3] = {{p0, m01, m20}, {m01, p1, m12}, {m20, m12, p2}, {m01, m12, m20}};
            for (const auto& tri : tris) {
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const glm::vec3& corner : tri) {
                    vertex({corner.x * radius, centreY + corner.y * radius * 0.85f, corner.z * radius}, corner);
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
            }
        }
    }
};

Mesh finish(Builder& builder, std::uint32_t trunkIndices, std::uint32_t foliageMaterial)
{
    Mesh mesh = std::move(builder.mesh);
    const auto total = static_cast<std::uint32_t>(mesh.indices.size());
    mesh.submeshes.push_back({0, trunkIndices, 0});
    mesh.submeshes.push_back({trunkIndices, total - trunkIndices, foliageMaterial});
    return mesh;
}

Builder start(const char* name)
{
    Builder builder;
    builder.mesh.name = name;
    builder.mesh.boundsMin = glm::vec3(1e30f);
    builder.mesh.boundsMax = glm::vec3(-1e30f);
    return builder;
}

}

void scatterVegetation(const Heightfield& heightfield, CellId id, const VegetationParams& params, std::vector<VegetationInstance>& out)
{
    out.clear();
    const WorldDesc& desc = heightfield.desc();
    const double originX = heightfield.cellOriginX(id);
    const double originZ = heightfield.cellOriginZ(id);
    const auto steps = static_cast<int>(std::ceil(static_cast<float>(desc.cellMetres) / params.spacing));
    const std::uint64_t cellSeed = hashCombine(hashCombine(desc.seed, static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.x))), static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.z)));

    for (int gz = 0; gz < steps; ++gz) {
        for (int gx = 0; gx < steps; ++gx) {
            // Each candidate has its own generator, so the draw for one never depends on another.
            Rng rng(hashCombine(cellSeed, static_cast<std::uint64_t>(gz) * 4096 + static_cast<std::uint64_t>(gx)));
            const float localX = (static_cast<float>(gx) + rng.nextFloat()) * params.spacing;
            const float localZ = (static_cast<float>(gz) + rng.nextFloat()) * params.spacing;
            const float acceptRoll = rng.nextFloat();
            const float yaw = rng.nextFloat() * kTwoPi;
            const float sizeRoll = rng.nextFloat();
            const float kindRoll = rng.nextFloat();
            if (localX >= static_cast<float>(desc.cellMetres) || localZ >= static_cast<float>(desc.cellMetres)) {
                continue;
            }
            const double worldX = originX + localX;
            const double worldZ = originZ + localZ;
            const float height = heightfield.heightAt(worldX, worldZ);
            const float above = height - desc.minHeight;
            if (above < params.minHeight || above > params.maxHeight) {
                continue;
            }
            if (1.0f - heightfield.normalAt(worldX, worldZ).y > params.maxSlope) {
                continue;
            }
            if (acceptRoll > forestDensity(worldX, worldZ, params.forestScale)) {
                continue;
            }
            VegetationInstance tree;
            tree.position = {localX, height, localZ};
            tree.yaw = yaw;
            tree.scale = 0.75f + sizeRoll * 0.7f;
            // Conifers favour altitude, broadleaves the lowlands, with a mix between.
            const float conifer = std::clamp((above - 40.0f) / 140.0f, 0.0f, 1.0f) * 0.9f + 0.05f;
            tree.kind = kindRoll < conifer ? static_cast<std::uint32_t>(VegetationKind::Conifer) : static_cast<std::uint32_t>(VegetationKind::Broadleaf);
            out.push_back(tree);
        }
    }
}

SceneBundle treeBundle()
{
    SceneBundle bundle;
    const auto material = [&](const char* name, glm::vec3 colour, float roughness) {
        Material m;
        m.name = name;
        m.baseColorFactor = glm::vec4(colour, 1.0f);
        m.metallicFactor = 0.0f;
        m.roughnessFactor = roughness;
        bundle.materials.push_back(m);
    };
    material("trunk", {0.30f, 0.19f, 0.10f}, 0.95f);
    material("conifer", {0.06f, 0.22f, 0.10f}, 0.9f);
    material("broadleaf", {0.16f, 0.34f, 0.08f}, 0.85f);

    {
        Builder b = start("conifer");
        b.frustum(0.0f, 2.0f, 0.22f, 0.16f, 6);
        const auto trunk = static_cast<std::uint32_t>(b.mesh.indices.size());
        b.frustum(1.2f, 3.6f, 1.5f, 0.55f, 8);
        b.frustum(2.6f, 5.2f, 1.15f, 0.35f, 8);
        b.frustum(4.2f, 6.8f, 0.8f, 0.0f, 8);
        bundle.meshes.push_back(finish(b, trunk, 1));
    }
    {
        Builder b = start("broadleaf");
        b.frustum(0.0f, 2.4f, 0.3f, 0.2f, 6);
        const auto trunk = static_cast<std::uint32_t>(b.mesh.indices.size());
        b.crown(3.6f, 2.0f);
        bundle.meshes.push_back(finish(b, trunk, 2));
    }
    return bundle;
}

}
