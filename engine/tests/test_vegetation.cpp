#include "ichor/world/forge.h"
#include "ichor/world/vegetation.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>

using namespace ichor;

namespace {

Heightfield world(std::uint64_t seed = 5)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return forgeHeightfield(desc);
}

}

TEST_CASE("vegetation is planted the same way every time, whatever else was asked for first")
{
    const Heightfield field = world();
    VegetationParams params;
    std::vector<VegetationInstance> first;
    std::vector<VegetationInstance> again;
    std::vector<VegetationInstance> other;
    scatterVegetation(field, {1, 1}, params, first);
    scatterVegetation(field, {2, 2}, params, other); // another cell in between changes nothing
    scatterVegetation(field, {1, 1}, params, again);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.size() == again.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].position == again[i].position);
        REQUIRE(first[i].yaw == again[i].yaw);
        REQUIRE(first[i].scale == again[i].scale);
        REQUIRE(first[i].kind == again[i].kind);
    }

    const Heightfield elsewhere = world(6);
    std::vector<VegetationInstance> differing;
    scatterVegetation(elsewhere, {1, 1}, params, differing);
    REQUIRE((differing.size() != first.size() || differing[0].position != first[0].position));
}

TEST_CASE("trees stand inside their cell, on ground that allows them")
{
    const Heightfield field = world();
    VegetationParams params;
    std::size_t total = 0;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            std::vector<VegetationInstance> trees;
            scatterVegetation(field, {x, z}, params, trees);
            total += trees.size();
            for (const VegetationInstance& tree : trees) {
                REQUIRE(tree.position.x >= 0.0f);
                REQUIRE(tree.position.x < 512.0f);
                REQUIRE(tree.position.z >= 0.0f);
                REQUIRE(tree.position.z < 512.0f);
                const double wx = field.cellOriginX({x, z}) + tree.position.x;
                const double wz = field.cellOriginZ({x, z}) + tree.position.z;
                REQUIRE(tree.position.y == field.heightAt(wx, wz));
                const float above = tree.position.y - field.desc().minHeight;
                REQUIRE(above >= params.minHeight);
                REQUIRE(above <= params.maxHeight);
                REQUIRE(1.0f - field.normalAt(wx, wz).y <= params.maxSlope);
                REQUIRE(tree.scale >= 0.75f);
                REQUIRE(tree.scale <= 1.45f);
                REQUIRE(tree.kind < static_cast<std::uint32_t>(VegetationKind::Count));
            }
        }
    }
    REQUIRE(total > 200);
}

TEST_CASE("tighter spacing plants more and a steep limit plants fewer")
{
    const Heightfield field = world();
    VegetationParams loose;
    VegetationParams dense;
    dense.spacing = 7.0f;
    VegetationParams flatOnly;
    flatOnly.maxSlope = 0.02f;
    std::vector<VegetationInstance> a;
    std::vector<VegetationInstance> b;
    std::vector<VegetationInstance> c;
    scatterVegetation(field, {1, 1}, loose, a);
    scatterVegetation(field, {1, 1}, dense, b);
    scatterVegetation(field, {1, 1}, flatOnly, c);
    REQUIRE(b.size() > a.size() * 2);
    REQUIRE(c.size() < a.size());
}

TEST_CASE("no tree grows below the water and the world's own water level is a field of its description")
{
    WorldDesc desc;
    desc.seed = 5;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    REQUIRE_FALSE(desc.hasWater());
    const Heightfield dry = forgeHeightfield(desc);
    desc.waterHeight = 120.0f;
    REQUIRE(desc.hasWater());
    REQUIRE(desc.waterLevel() == desc.minHeight + 120.0f);
    const Heightfield flooded(desc, dry.samples());

    std::vector<VegetationInstance> before;
    std::vector<VegetationInstance> after;
    scatterVegetation(dry, {1, 1}, VegetationParams{}, before);
    scatterVegetation(flooded, {1, 1}, VegetationParams{}, after);
    REQUIRE(after.size() < before.size());
    for (const VegetationInstance& tree : after) {
        REQUIRE(tree.position.y >= desc.waterLevel());
    }
}

TEST_CASE("the tree meshes are closed over valid indices and face outward")
{
    const SceneBundle bundle = treeBundle();
    REQUIRE(bundle.meshes.size() == static_cast<std::size_t>(VegetationKind::Count));
    REQUIRE(bundle.materials.size() == 3);
    for (const Mesh& mesh : bundle.meshes) {
        INFO(mesh.name);
        REQUIRE(mesh.indices.size() % 3 == 0);
        REQUIRE(mesh.submeshes.size() == 2);
        REQUIRE(mesh.submeshes[0].firstIndex == 0);
        REQUIRE(mesh.submeshes[0].firstIndex + mesh.submeshes[0].indexCount == mesh.submeshes[1].firstIndex);
        REQUIRE(mesh.submeshes[1].firstIndex + mesh.submeshes[1].indexCount == mesh.indices.size());
        for (const Submesh& submesh : mesh.submeshes) {
            REQUIRE(submesh.material < bundle.materials.size());
            REQUIRE(submesh.indexCount > 0);
        }
        for (std::uint32_t index : mesh.indices) {
            REQUIRE(index < mesh.vertices.size());
        }
        for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
            const Vertex& a = mesh.vertices[mesh.indices[t]];
            const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[t + 2]];
            const glm::vec3 geometric = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::length(geometric) < 1e-6f) {
                continue; // the cone's tip collapses a side to a point
            }
            REQUIRE(glm::dot(geometric, a.normal + b.normal + c.normal) > 0.0f);
        }
        REQUIRE(mesh.boundsMin.y >= 0.0f);
        REQUIRE(mesh.boundsMax.y > 3.0f);
    }
}
