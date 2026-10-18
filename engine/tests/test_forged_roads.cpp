#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_roads.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace ichor;

namespace {

constexpr int kGrid = 129;

WorldDesc roadWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return forgedWorldDesc(desc);
}

worldforge::ForgedWorld forgedFor(const WorldDesc& desc, worldforge::ForgePhase stop = worldforge::ForgePhase::Roads)
{
    return worldforge::forgeWorld(desc.seed, kGrid, {}, stop);
}

// A seed whose world has roads: the forge may place none on a world with too few settlements.
struct Fixture {
    WorldDesc desc;
    worldforge::ForgedWorld world;
};

Fixture withRoads()
{
    for (std::uint64_t seed = 1; seed < 40; ++seed) {
        const WorldDesc desc = roadWorld(seed);
        worldforge::ForgedWorld world = forgedFor(desc);
        if (world.roads().has_value() && !world.roads()->edges.empty()) return {desc, std::move(world)};
    }
    FAIL("no seed forged roads");
    return {roadWorld(1), forgedFor(roadWorld(1))};
}

struct Point {
    double x;
    double z;
};

Point cellPoint(const worldforge::RoadMap& map, const WorldDesc& desc, std::uint32_t cell)
{
    const double size = static_cast<double>(desc.sizeMetres) / (kGrid - 1);
    return {static_cast<double>(cell % kGrid) * size, static_cast<double>(cell / kGrid) * size};
}

// The mean absolute second difference of the height along every road path: how bumpy the roads are.
double roughnessAlongRoads(const worldforge::RoadMap& map, const WorldDesc& desc, const Heightfield& field)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (const worldforge::RoadEdge& edge : map.edges) {
        for (std::uint32_t i = 1; i + 1 < edge.pathCount; ++i) {
            const auto height = [&](std::uint32_t k) {
                const Point p = cellPoint(map, desc, map.pathCells[edge.pathBegin + k]);
                return static_cast<double>(field.heightAt(p.x, p.z));
            };
            sum += std::abs(height(i - 1) - 2.0 * height(i) + height(i + 1));
            ++count;
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

}

TEST_CASE("the road terrain is flatter along the roads than the hydrology terrain")
{
    const Fixture f = withRoads();
    const worldforge::RoadMap& map = *f.world.roads();
    const ForgedHeightfield plain = heightfieldFromForgedWorld(f.world, f.desc);
    const ForgedHeightfield deformed = heightfieldFromForgedRoads(f.world, f.desc);
    REQUIRE(plain.ok());
    REQUIRE(deformed.ok());

    const double before = roughnessAlongRoads(map, f.desc, plain.field);
    const double after = roughnessAlongRoads(map, f.desc, deformed.field);
    CHECK(after < before * 0.8);
}

TEST_CASE("ground far from any road is the same with and without the deformation")
{
    const Fixture f = withRoads();
    const ForgedHeightfield plain = heightfieldFromForgedWorld(f.world, f.desc);
    const ForgedHeightfield deformed = heightfieldFromForgedRoads(f.world, f.desc);
    REQUIRE(plain.ok());
    REQUIRE(deformed.ok());

    const worldforge::RoadMap& map = *f.world.roads();
    const double cellSize = static_cast<double>(f.desc.sizeMetres) / (kGrid - 1);
    const auto nearRoadCell = [&](double x, double z, int radius) {
        const int cx = static_cast<int>(std::lround(x / cellSize));
        const int cz = static_cast<int>(std::lround(z / cellSize));
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int ix = cx + dx;
                const int iz = cz + dz;
                if (ix >= 0 && iz >= 0 && ix < kGrid && iz < kGrid && map.road[static_cast<std::size_t>(iz) * kGrid + ix] != 0) return true;
            }
        }
        return false;
    };

    const double step = f.desc.metresPerSample;
    std::size_t compared = 0;
    std::size_t changedNearRoads = 0;
    for (double z = 0.0; z <= f.desc.sizeMetres; z += step) {
        for (double x = 0.0; x <= f.desc.sizeMetres; x += step) {
            const bool changed = plain.field.heightAt(x, z) != deformed.field.heightAt(x, z);
            if (nearRoadCell(x, z, 3)) {
                changedNearRoads += changed ? 1 : 0;
                continue;
            }
            ++compared;
            CHECK_FALSE(changed);
        }
    }
    CHECK(compared > 1000);
    CHECK(changedNearRoads > 0);
}

TEST_CASE("a point on a road is reported as road and a point far off it is not")
{
    const Fixture f = withRoads();
    const worldforge::RoadMap& map = *f.world.roads();
    const ForgedRoads roads(f.world, f.desc);
    REQUIRE_FALSE(roads.empty());

    for (const worldforge::RoadEdge& edge : map.edges) {
        for (std::uint32_t i = 0; i < edge.pathCount; i += 3) {
            const Point p = cellPoint(map, f.desc, map.pathCells[edge.pathBegin + i]);
            const RoadHit hit = roads.query(p.x, p.z);
            CHECK(hit.coverage == Catch::Approx(1.0f));
            CHECK(hit.edge >= 0);
        }
    }

    const double cell = static_cast<double>(f.desc.sizeMetres) / (kGrid - 1);
    std::size_t offRoad = 0;
    for (std::uint32_t c = 0; c < map.road.size(); ++c) {
        if (map.road[c] != 0) continue;
        const Point p = cellPoint(map, f.desc, c);
        bool clear = true;
        for (int dz = -2; dz <= 2 && clear; ++dz) {
            for (int dx = -2; dx <= 2 && clear; ++dx) {
                const int x = static_cast<int>(c % kGrid) + dx;
                const int z = static_cast<int>(c / kGrid) + dz;
                if (x >= 0 && z >= 0 && x < kGrid && z < kGrid && map.road[static_cast<std::size_t>(z) * kGrid + x] != 0) clear = false;
            }
        }
        if (!clear) continue;
        ++offRoad;
        const RoadHit hit = roads.query(p.x, p.z);
        CHECK(hit.coverage == 0.0f);
        CHECK(hit.edge == -1);
    }
    CHECK(offRoad > 100);
    CHECK(cell > 0.0);
}

TEST_CASE("coverage falls off across the shoulder and never leaves 0 to 1")
{
    const Fixture f = withRoads();
    const worldforge::RoadMap& map = *f.world.roads();
    const ForgedRoads roads(f.world, f.desc);
    const worldforge::RoadEdge& edge = map.edges.front();
    const Point a = cellPoint(map, f.desc, map.pathCells[edge.pathBegin]);

    float last = 2.0f;
    for (double d = 0.0; d <= 12.0; d += 0.5) {
        for (const double dir : {1.0, -1.0}) {
            const float c = roads.coverageAt(a.x + dir * d, a.z);
            CHECK(c >= 0.0f);
            CHECK(c <= 1.0f);
        }
        last = std::min(last, roads.coverageAt(a.x + d, a.z));
    }
    CHECK(last == 0.0f);
}

TEST_CASE("the road query reports each edge with a length")
{
    const Fixture f = withRoads();
    const worldforge::RoadMap& map = *f.world.roads();
    const ForgedRoads roads(f.world, f.desc);
    const Point p = cellPoint(map, f.desc, map.pathCells[map.edges.front().pathBegin]);
    const RoadHit hit = roads.query(p.x, p.z);
    REQUIRE(hit.edge >= 0);
    CHECK(roads.edgeLengthMetres(hit.edge) > 0.0);
    CHECK(roads.edgeLengthMetres(-1) == 0.0);
    CHECK(roads.edgeLengthMetres(1 << 20) == 0.0);
}

TEST_CASE("a world that stopped before roads has no roads and bad input is refused")
{
    const WorldDesc desc = roadWorld(3);
    const worldforge::ForgedWorld early = forgedFor(desc, worldforge::ForgePhase::Hydrology);
    CHECK(ForgedRoads(early, desc).empty());
    CHECK_FALSE(heightfieldFromForgedRoads(early, desc).ok());

    const Fixture f = withRoads();
    ForgedRoadParams nan;
    nan.widthMetres = std::numeric_limits<float>::quiet_NaN();
    CHECK(ForgedRoads(f.world, f.desc, nan).empty());
    ForgedRoadParams zero;
    zero.widthMetres = 0.0f;
    CHECK(ForgedRoads(f.world, f.desc, zero).empty());

    WorldDesc broken = f.desc;
    broken.cellMetres = 0;
    CHECK(ForgedRoads(f.world, broken).empty());
    CHECK_FALSE(heightfieldFromForgedRoads(f.world, broken).ok());

    const ForgedRoads roads(f.world, f.desc);
    CHECK(roads.coverageAt(std::numeric_limits<double>::quiet_NaN(), 0.0) == 0.0f);
    CHECK(roads.coverageAt(std::numeric_limits<double>::infinity(), 5.0) == 0.0f);
    CHECK(roads.coverageAt(-1e9, 1e9) == 0.0f);
}

TEST_CASE("a cell with a road gets a ribbon lying on the ground, a cell without gets none")
{
    const Fixture f = withRoads();
    const ForgedRoads roads(f.world, f.desc);
    const ForgedHeightfield deformed = heightfieldFromForgedRoads(f.world, f.desc);
    REQUIRE(deformed.ok());

    std::size_t withMesh = 0;
    std::size_t without = 0;
    const std::int32_t cells = static_cast<std::int32_t>(f.desc.cellsPerSide());
    for (std::int32_t z = 0; z < cells; ++z) {
        for (std::int32_t x = 0; x < cells; ++x) {
            const CellId id{x, z};
            const Mesh mesh = roads.buildMesh(deformed.field, id);
            if (mesh.vertices.empty()) {
                ++without;
                CHECK(mesh.indices.empty());
                continue;
            }
            ++withMesh;
            CHECK(mesh.indices.size() % 3 == 0);
            const double ox = deformed.field.cellOriginX(id);
            const double oz = deformed.field.cellOriginZ(id);
            for (const Vertex& v : mesh.vertices) {
                const float margin = static_cast<float>(f.desc.metresPerSample);
                CHECK(v.position.x >= -margin);
                CHECK(v.position.x <= static_cast<float>(f.desc.cellMetres) + margin);
                CHECK(v.position.z >= -margin);
                CHECK(v.position.z <= static_cast<float>(f.desc.cellMetres) + margin);
                const float ground = deformed.field.heightAt(ox + v.position.x, oz + v.position.z);
                CHECK(v.position.y == Catch::Approx(ground + 0.08f).margin(1e-3));
            }
            for (const std::uint32_t i : mesh.indices) CHECK(i < mesh.vertices.size());
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
                const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
                const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
                CHECK(glm::cross(b - a, c - a).y >= 0.0f);
            }
        }
    }
    CHECK(withMesh > 0);
    CHECK(without > 0);
}

TEST_CASE("the ribbons together cover no more than the roads once each")
{
    const Fixture f = withRoads();
    const ForgedRoads roads(f.world, f.desc);
    const ForgedHeightfield deformed = heightfieldFromForgedRoads(f.world, f.desc);
    REQUIRE(deformed.ok());

    const std::int32_t cells = static_cast<std::int32_t>(f.desc.cellsPerSide());
    double area = 0.0;
    for (std::int32_t z = 0; z < cells; ++z) {
        for (std::int32_t x = 0; x < cells; ++x) {
            const Mesh mesh = roads.buildMesh(deformed.field, {x, z});
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
                const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
                const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
                area += 0.5 * std::abs((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z));
            }
        }
    }
    double length = 0.0;
    for (std::size_t e = 0; e < f.world.roads()->edges.size(); ++e) length += roads.edgeLengthMetres(static_cast<int>(e));
    // The merged routes are drawn once, so the ribbon is no longer than the sum of the edges times the width.
    CHECK(area > 0.0);
    CHECK(area <= length * 4.0 * 1.05);
}
