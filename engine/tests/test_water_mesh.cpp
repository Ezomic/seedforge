#include "ichor/world/forge.h"
#include "ichor/world/streamer.h"
#include "ichor/world/water_mesh.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

// A square heightfield n samples to a side, one metre apart, one cell, heights in tenths of a metre.
Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

std::uint16_t basinWorld(std::uint32_t x, std::uint32_t z)
{
    if (x == 16 && z == 8) return 5000;
    if (x >= 5 && x <= 11 && z >= 5 && z <= 11) return 1000;
    return 10000;
}

WorldDesc forgedDesc()
{
    WorldDesc desc;
    desc.seed = 11;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

void requireWellFormed(const Mesh& mesh)
{
    REQUIRE(mesh.indices.size() % 3 == 0);
    REQUIRE(mesh.submeshes.size() == 1);
    REQUIRE(mesh.submeshes[0].firstIndex == 0);
    REQUIRE(mesh.submeshes[0].indexCount == mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        REQUIRE(index < mesh.vertices.size());
    }
    for (const Vertex& v : mesh.vertices) {
        REQUIRE(v.normal == glm::vec3(0.0f, 1.0f, 0.0f));
        REQUIRE(std::isfinite(v.position.y));
        REQUIRE(v.position.x >= mesh.boundsMin.x);
        REQUIRE(v.position.x <= mesh.boundsMax.x);
        REQUIRE(v.position.y >= mesh.boundsMin.y);
        REQUIRE(v.position.y <= mesh.boundsMax.y);
    }
    // Seen from above, every triangle is counter-clockwise (or has no area): the surface faces up.
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
        const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
        const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
        REQUIRE(glm::cross(b - a, c - a).y >= 0.0f);
    }
}

}

TEST_CASE("a dry cell has no water mesh, and neither does a mismatched hydrology")
{
    const Heightfield slope = gridOf(17, [](std::uint32_t x, std::uint32_t z) { return static_cast<std::uint16_t>(x * 100 + z % 3); });
    const Hydrology h = computeHydrology(slope);
    HydrologyParams rivers;
    rivers.riverThreshold = 100000; // no river is that big here
    REQUIRE(buildWaterMesh(slope, h, rivers, {0, 0}).vertices.empty());

    const Heightfield basin = gridOf(17, basinWorld);
    const Hydrology wet = computeHydrology(basin);
    REQUIRE_FALSE(buildWaterMesh(basin, wet, rivers, {0, 0}).vertices.empty());
    REQUIRE(buildWaterMesh(basin, wet, rivers, {3, 3}).vertices.empty());   // no such cell
    const Heightfield small = gridOf(9, [](std::uint32_t, std::uint32_t) { return std::uint16_t{0}; });
    REQUIRE(buildWaterMesh(basin, computeHydrology(small), rivers, {0, 0}).vertices.empty()); // a hydrology of another world
    REQUIRE(buildWaterMesh(basin, Hydrology{}, rivers, {0, 0}).vertices.empty());
    REQUIRE(buildWaterMesh(Heightfield{}, wet, rivers, {0, 0}).vertices.empty());
}

TEST_CASE("a lake's surface lies at its filled level, and ends below the bank where the ground rises")
{
    const Heightfield field = gridOf(17, basinWorld);
    const Hydrology h = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 100000; // lakes only
    const WaterMeshParams params;
    const Mesh mesh = buildWaterMesh(field, h, rivers, {0, 0}, params);
    REQUIRE_FALSE(mesh.vertices.empty());
    requireWellFormed(mesh);

    // The basin is a lake; its samples sit at the level the flood filled them to (the plateau).
    const float lake = h.filled[h.index(8, 8)];
    std::size_t atLake = 0;
    for (const Vertex& v : mesh.vertices) {
        const auto x = static_cast<std::uint32_t>(v.position.x);
        const auto z = static_cast<std::uint32_t>(v.position.z);
        if (isLake(h, field, x, z, rivers)) {
            REQUIRE(v.position.y == lake);
            ++atLake;
        } else {
            // A dry corner of a drawn quad is sunk below the ground there.
            REQUIRE(v.position.y == field.heightAtSample(x, z) - params.submerge);
        }
    }
    REQUIRE(atLake == 7u * 7u);
    // The mesh reaches one sample beyond the lake all round, and no further.
    REQUIRE(mesh.boundsMin.x == 4.0f);
    REQUIRE(mesh.boundsMax.x == 12.0f);
    REQUIRE(mesh.boundsMin.z == 4.0f);
    REQUIRE(mesh.boundsMax.z == 12.0f);
}

TEST_CASE("rivers get a surface over their bed, and a cell built twice is the same")
{
    const Heightfield field = forgeHeightfield(forgedDesc());
    const Hydrology h = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 200;
    const WaterMeshParams params;
    std::size_t withWater = 0;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            const CellId id{x, z};
            const Mesh a = buildWaterMesh(field, h, rivers, id, params);
            const Mesh b = buildWaterMesh(field, h, rivers, id, params);
            REQUIRE(a.vertices.size() == b.vertices.size());
            REQUIRE(a.indices == b.indices);
            if (a.vertices.empty()) continue;
            ++withWater;
            requireWellFormed(a);
            // A river sample's vertex is the bed plus the surface offset, a lake's is the filled level.
            for (const Vertex& v : a.vertices) {
                const auto gx = static_cast<std::uint32_t>(x) * 32 + static_cast<std::uint32_t>(v.position.x / 8.0f);
                const auto gz = static_cast<std::uint32_t>(z) * 32 + static_cast<std::uint32_t>(v.position.z / 8.0f);
                const float ground = field.heightAtSample(gx, gz);
                if (isLake(h, field, gx, gz, rivers)) {
                    REQUIRE(v.position.y == h.filled[h.index(gx, gz)]);
                } else if (isRiver(h, h.index(gx, gz), rivers)) {
                    REQUIRE(v.position.y == ground + params.riverSurface);
                } else {
                    REQUIRE(v.position.y == ground - params.submerge);
                }
            }
        }
    }
    REQUIRE(withWater > 0);
}

TEST_CASE("neighbouring cells put the water at the same level along their shared edge")
{
    const Heightfield field = forgeHeightfield(forgedDesc());
    const Hydrology h = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 200;
    std::size_t compared = 0;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x + 1 < 4; ++x) {
            const Mesh left = buildWaterMesh(field, h, rivers, {x, z});
            const Mesh right = buildWaterMesh(field, h, rivers, {x + 1, z});
            std::map<float, float> rightEdge; // z along the edge -> level, on the right cell's west side
            for (const Vertex& v : right.vertices) {
                if (v.position.x == 0.0f) rightEdge[v.position.z] = v.position.y;
            }
            for (const Vertex& v : left.vertices) {
                if (v.position.x != 256.0f) continue;
                const auto found = rightEdge.find(v.position.z);
                if (found != rightEdge.end()) {
                    REQUIRE(found->second == v.position.y);
                    ++compared;
                }
            }
        }
    }
    REQUIRE(compared > 0);
}

TEST_CASE("the streamer builds the water of each cell when asked, and not otherwise")
{
    const Heightfield field = forgeHeightfield(forgedDesc());
    const auto streamOnce = [&](bool water) {
        StreamerConfig config;
        config.synchronous = true;
        config.radiusCells = 1;
        config.requiredRadius = 1;
        config.activationsPerUpdate = 100;
        config.water = water;
        config.rivers.riverThreshold = 200;
        WorldStreamer streamer(field, config);
        std::size_t vertices = 0;
        streamer.onLoaded([&](const CellPayload& payload) { vertices += payload.water.vertices.size(); });
        for (int i = 0; i < 4; ++i) {
            streamer.update({512.0, 0.0, 512.0});
        }
        return vertices;
    };
    REQUIRE(streamOnce(false) == 0);
    REQUIRE(streamOnce(true) > 0);

    // A payload built straight from the world's hydrology agrees with what the streamer built.
    StreamerConfig config;
    config.water = true;
    config.rivers.riverThreshold = 200;
    const Hydrology h = computeHydrology(field);
    CellPayload payload;
    WorldStreamer::buildPayload(field, {1, 1}, config, payload, &h);
    REQUIRE(payload.water.vertices.size() == buildWaterMesh(field, h, config.rivers, {1, 1}).vertices.size());
    // Without the world's hydrology there is no water, even when asked for.
    WorldStreamer::buildPayload(field, {1, 1}, config, payload, nullptr);
    REQUIRE(payload.water.vertices.empty());
}
