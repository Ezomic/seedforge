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
                if (tree.kind == static_cast<std::uint32_t>(VegetationKind::Rock)) {
                    continue; // rocks have rules of their own, below
                }
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
    // Flooding only removes trees: every survivor is a tree the dry world had, unchanged.
    std::size_t next = 0;
    for (const VegetationInstance& tree : after) {
        while (next < before.size() && before[next].position != tree.position) {
            ++next;
        }
        REQUIRE(next < before.size());
        REQUIRE(before[next].kind == tree.kind);
        REQUIRE(before[next].yaw == tree.yaw);
        REQUIRE(before[next].scale == tree.scale);
    }
    for (const VegetationInstance& tree : after) {
        REQUIRE(tree.position.y >= desc.waterLevel());
    }
}

TEST_CASE("the tree meshes are closed over valid indices and face outward")
{
    const SceneBundle bundle = treeBundle();
    REQUIRE(bundle.meshes.size() == static_cast<std::size_t>(VegetationKind::Count));
    REQUIRE(bundle.materials.size() == 4); // trunk, conifer, broadleaf, rock
    for (const Mesh& mesh : bundle.meshes) {
        INFO(mesh.name);
        const bool rock = mesh.name == "rock";
        REQUIRE(mesh.indices.size() % 3 == 0);
        REQUIRE(mesh.submeshes.size() == (rock ? 1u : 2u)); // a rock is all one material
        REQUIRE(mesh.submeshes[0].firstIndex == 0);
        if (!rock) {
            REQUIRE(mesh.submeshes[0].firstIndex + mesh.submeshes[0].indexCount == mesh.submeshes[1].firstIndex);
        }
        REQUIRE(mesh.submeshes.back().firstIndex + mesh.submeshes.back().indexCount == mesh.indices.size());
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
        if (rock) {
            // Half buried: it reaches below the ground it is set on, and stands a little over it.
            REQUIRE(mesh.boundsMin.y < 0.0f);
            REQUIRE(mesh.boundsMax.y > 0.4f);
            REQUIRE(mesh.boundsMax.y < 1.2f);
        } else {
            REQUIRE(mesh.boundsMin.y >= 0.0f);
            REQUIRE(mesh.boundsMax.y > 3.0f);
        }
    }
}

TEST_CASE("rocks sit on the ground inside their cell, never in the water, and leave the trees as they were")
{
    const Heightfield field = world();
    VegetationParams withRocks;
    VegetationParams noRocks;
    noRocks.rockChance = 0.0f;
    noRocks.rockSteepBoost = 0.0f;
    const auto rockKind = static_cast<std::uint32_t>(VegetationKind::Rock);
    std::size_t rocks = 0;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            std::vector<VegetationInstance> all;
            std::vector<VegetationInstance> trees;
            scatterVegetation(field, {x, z}, withRocks, all);
            scatterVegetation(field, {x, z}, noRocks, trees);
            std::vector<VegetationInstance> justTrees;
            for (const VegetationInstance& plant : all) {
                if (plant.kind == rockKind) {
                    ++rocks;
                    REQUIRE(plant.position.x >= 0.0f);
                    REQUIRE(plant.position.x < 512.0f);
                    REQUIRE(plant.position.z >= 0.0f);
                    REQUIRE(plant.position.z < 512.0f);
                    const double wx = field.cellOriginX({x, z}) + plant.position.x;
                    const double wz = field.cellOriginZ({x, z}) + plant.position.z;
                    REQUIRE(plant.position.y == field.heightAt(wx, wz));
                    REQUIRE(plant.scale >= 0.4f);
                    REQUIRE(plant.scale <= 2.8f);
                } else {
                    justTrees.push_back(plant);
                }
            }
            // Asking for rocks changes no tree: same trees, in the same order, with the rocks after them.
            REQUIRE(justTrees.size() == trees.size());
            for (std::size_t i = 0; i < trees.size(); ++i) {
                REQUIRE(justTrees[i].position == trees[i].position);
                REQUIRE(justTrees[i].kind == trees[i].kind);
            }
            for (std::size_t i = justTrees.size(); i < all.size(); ++i) {
                REQUIRE(all[i].kind == rockKind); // rocks come last
            }
        }
    }
    REQUIRE(rocks > 100);

    // Not under a lake.
    WorldDesc desc = field.desc();
    desc.waterHeight = 120.0f;
    const Heightfield flooded(desc, field.samples());
    std::vector<VegetationInstance> plants;
    scatterVegetation(flooded, {1, 1}, withRocks, plants);
    for (const VegetationInstance& plant : plants) {
        REQUIRE(plant.position.y >= desc.waterLevel());
    }
}

TEST_CASE("rocks gather on steep ground, and a cell grows the same rocks every time")
{
    const Heightfield field = world();
    const VegetationParams params;
    const auto rockKind = static_cast<std::uint32_t>(VegetationKind::Rock);
    std::size_t rocks = 0;
    std::size_t steepRocks = 0;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            std::vector<VegetationInstance> first;
            std::vector<VegetationInstance> again;
            scatterVegetation(field, {x, z}, params, first);
            scatterVegetation(field, {x, z}, params, again);
            REQUIRE(first.size() == again.size());
            for (std::size_t i = 0; i < first.size(); ++i) {
                REQUIRE(first[i].position == again[i].position);
                REQUIRE(first[i].scale == again[i].scale);
            }
            for (const VegetationInstance& plant : first) {
                if (plant.kind != rockKind) continue;
                ++rocks;
                const double wx = field.cellOriginX({x, z}) + plant.position.x;
                const double wz = field.cellOriginZ({x, z}) + plant.position.z;
                steepRocks += 1.0f - field.normalAt(wx, wz).y > 0.2f ? 1 : 0;
            }
        }
    }
    std::size_t ground = 0;
    std::size_t steepGround = 0;
    for (double z = 8.0; z < 2040.0; z += 16.0) {
        for (double x = 8.0; x < 2040.0; x += 16.0) {
            ++ground;
            steepGround += 1.0f - field.normalAt(x, z).y > 0.2f ? 1 : 0;
        }
    }
    REQUIRE(steepGround > 0);
    REQUIRE(rocks > 0);
    const double rockShare = static_cast<double>(steepRocks) / static_cast<double>(rocks);
    const double groundShare = static_cast<double>(steepGround) / static_cast<double>(ground);
    REQUIRE(rockShare > groundShare * 1.5); // rock shows where the slope is
    REQUIRE(vegetationUnblendedKinds() == (1u << rockKind));
}
