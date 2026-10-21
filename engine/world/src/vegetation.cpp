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

float forestDensity(double x, double z, float scale)
{
    return forestPatch(x, z, scale);
}

// Stone is never smooth: each corner of a rounded blob is pushed in or out by a repeatable amount.
float rockiness(glm::vec3 corner)
{
    const auto q = [](float v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(v * 1000.0f)) + 100000); };
    const std::uint64_t h = hashCombine(hashCombine(q(corner.x), q(corner.y)), q(corner.z));
    return 0.8f + 0.35f * static_cast<float>(h % 1000) / 999.0f;
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

    // A cone or cylinder frustum along any axis, from one point to another: a branch, a blade, an arm.
    void stick(glm::vec3 from, glm::vec3 to, float fromRadius, float toRadius, int sides)
    {
        const glm::vec3 axis = glm::normalize(to - from);
        const glm::vec3 across = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 up = glm::cross(across, axis);
        const float slope = (fromRadius - toRadius) / glm::length(to - from);
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i <= sides; ++i) {
            const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
            const glm::vec3 radial = across * dmath::cos(angle) + up * dmath::sin(angle);
            const glm::vec3 normal = radial + axis * slope;
            vertex(from + radial * fromRadius, normal);
            vertex(to + radial * toRadius, normal);
        }
        for (int i = 0; i < sides; ++i) {
            const std::uint32_t a = first + static_cast<std::uint32_t>(i) * 2;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
        }
    }

    // A boulder: the same rounded blob as a crown, lumpy, flattened, and set so it is half buried.
    void boulder(float radius)
    {
        const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int faces[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
        for (const auto& face : faces) {
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
                    const float lump = rockiness(corner);
                    vertex({corner.x * radius * lump, radius * 0.25f + corner.y * radius * 0.62f * lump, corner.z * radius * lump}, corner);
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
            }
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

// A smooth patchwork of forest and clearing: a few overlapping waves of
// distance, so the density has no grid in it. Deterministic through dmath.
float forestPatch(double x, double z, float scale)
{
    const auto fx = static_cast<float>(x / scale);
    const auto fz = static_cast<float>(z / scale);
    const float a = dmath::sin(fx * 1.7f + 0.3f) * dmath::sin(fz * 1.3f + 1.1f);
    const float b = dmath::sin(fx * 3.1f + fz * 2.3f + 2.0f);
    const float value = 0.5f + 0.35f * a + 0.15f * b;
    return std::clamp((value - 0.25f) * 1.8f, 0.0f, 1.0f);
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
            // Trees stand on the bank, not in the water.
            if (desc.hasWater() && height < desc.waterLevel() + params.bankMargin) {
                continue;
            }
            if (1.0f - heightfield.normalAt(worldX, worldZ).y > params.maxSlope) {
                continue;
            }
            if (acceptRoll > forestDensity(worldX, worldZ, params.forestScale) * params.forestDensity) {
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

    // Rocks, on a grid of their own with their own generators: more of them where the ground is steep.
    const auto rockSteps = static_cast<int>(std::ceil(static_cast<float>(desc.cellMetres) / params.rockSpacing));
    const std::uint64_t rockSeed = hashCombine(cellSeed, 0x726f636bULL);
    for (int gz = 0; gz < rockSteps; ++gz) {
        for (int gx = 0; gx < rockSteps; ++gx) {
            Rng rng(hashCombine(rockSeed, static_cast<std::uint64_t>(gz) * 4096 + static_cast<std::uint64_t>(gx)));
            const float localX = (static_cast<float>(gx) + rng.nextFloat()) * params.rockSpacing;
            const float localZ = (static_cast<float>(gz) + rng.nextFloat()) * params.rockSpacing;
            const float acceptRoll = rng.nextFloat();
            const float yaw = rng.nextFloat() * kTwoPi;
            const float sizeRoll = rng.nextFloat();
            if (localX >= static_cast<float>(desc.cellMetres) || localZ >= static_cast<float>(desc.cellMetres)) {
                continue;
            }
            const double worldX = originX + localX;
            const double worldZ = originZ + localZ;
            const float height = heightfield.heightAt(worldX, worldZ);
            if (desc.hasWater() && height < desc.waterLevel() + params.bankMargin) {
                continue;
            }
            const float slope = 1.0f - heightfield.normalAt(worldX, worldZ).y;
            const float steep = std::clamp((slope - 0.12f) / 0.28f, 0.0f, 1.0f);
            if (acceptRoll > params.rockChance + params.rockSteepBoost * steep * steep * (3.0f - 2.0f * steep)) {
                continue;
            }
            VegetationInstance rock;
            rock.position = {localX, height, localZ};
            rock.yaw = yaw;
            // Many small stones and a few big ones.
            rock.scale = 0.4f + sizeRoll * sizeRoll * 2.4f;
            rock.kind = static_cast<std::uint32_t>(VegetationKind::Rock);
            out.push_back(rock);
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
    material("rock", {0.34f, 0.33f, 0.31f}, 0.95f);
    material("grass", {0.20f, 0.42f, 0.10f}, 0.9f);
    material("bush", {0.12f, 0.30f, 0.09f}, 0.9f);
    material("deadwood", {0.32f, 0.27f, 0.22f}, 0.95f);
    material("cactus", {0.16f, 0.36f, 0.20f}, 0.85f);
    material("dry_shrub", {0.45f, 0.36f, 0.18f}, 0.95f);
    material("reed", {0.42f, 0.46f, 0.20f}, 0.9f);

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
    {
        Builder b = start("rock");
        b.boulder(1.0f);
        Mesh mesh = std::move(b.mesh);
        mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 3});
        bundle.meshes.push_back(std::move(mesh));
    }
    const auto plain = [&](Builder& b, std::uint32_t materialIndex) {
        Mesh mesh = std::move(b.mesh);
        mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), materialIndex});
        bundle.meshes.push_back(std::move(mesh));
    };
    {
        Builder b = start("grass");
        b.stick({0.0f, 0.0f, 0.0f}, {0.05f, 0.7f, 0.0f}, 0.09f, 0.0f, 3);
        b.stick({0.12f, 0.0f, 0.06f}, {0.34f, 0.5f, 0.12f}, 0.08f, 0.0f, 3);
        b.stick({-0.1f, 0.0f, 0.08f}, {-0.3f, 0.55f, 0.14f}, 0.08f, 0.0f, 3);
        b.stick({0.02f, 0.0f, -0.1f}, {-0.04f, 0.6f, -0.3f}, 0.08f, 0.0f, 3);
        plain(b, 4);
    }
    {
        Builder b = start("bush");
        b.frustum(0.0f, 0.35f, 0.07f, 0.05f, 4);
        const auto trunk = static_cast<std::uint32_t>(b.mesh.indices.size());
        b.crown(0.7f, 0.85f);
        bundle.meshes.push_back(finish(b, trunk, 5));
    }
    {
        Builder b = start("dead_tree");
        b.stick({0.0f, 0.0f, 0.0f}, {0.15f, 4.4f, 0.0f}, 0.26f, 0.06f, 5);
        b.stick({0.05f, 2.0f, 0.0f}, {1.3f, 3.4f, 0.3f}, 0.1f, 0.03f, 4);
        b.stick({0.1f, 3.0f, 0.0f}, {-1.0f, 4.2f, -0.4f}, 0.08f, 0.03f, 4);
        b.stick({0.1f, 3.7f, 0.0f}, {0.6f, 5.0f, 0.6f}, 0.05f, 0.02f, 4);
        plain(b, 6);
    }
    {
        Builder b = start("cactus");
        b.stick({0.0f, 0.0f, 0.0f}, {0.0f, 2.8f, 0.0f}, 0.24f, 0.16f, 6);
        b.stick({0.1f, 1.1f, 0.0f}, {0.85f, 1.4f, 0.0f}, 0.12f, 0.11f, 5);
        b.stick({0.85f, 1.3f, 0.0f}, {0.9f, 2.3f, 0.0f}, 0.11f, 0.08f, 5);
        b.stick({-0.1f, 1.7f, 0.0f}, {-0.7f, 1.95f, 0.0f}, 0.1f, 0.09f, 5);
        b.stick({-0.7f, 1.9f, 0.0f}, {-0.75f, 2.6f, 0.0f}, 0.09f, 0.07f, 5);
        plain(b, 7);
    }
    {
        Builder b = start("dry_shrub");
        b.crown(0.28f, 0.55f);
        plain(b, 8);
    }
    {
        Builder b = start("reed");
        b.stick({0.0f, 0.0f, 0.0f}, {0.04f, 1.7f, 0.02f}, 0.05f, 0.0f, 3);
        b.stick({0.14f, 0.0f, 0.04f}, {0.3f, 1.35f, 0.1f}, 0.045f, 0.0f, 3);
        b.stick({-0.12f, 0.0f, 0.06f}, {-0.3f, 1.5f, 0.1f}, 0.045f, 0.0f, 3);
        b.stick({0.0f, 0.0f, -0.12f}, {-0.06f, 1.25f, -0.28f}, 0.045f, 0.0f, 3);
        plain(b, 9);
    }
    return bundle;
}

}
