#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/water_field.h"
#include "ichor/world/water_mesh.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

using namespace ichor;

namespace {

constexpr int kGrid = 129;
constexpr std::uint32_t kStride = 2;

struct Fixture {
    WorldDesc desc;
    worldforge::ForgedWorld world;
    Heightfield field;
    ForgedWater water;
};

Fixture makeFixture(std::uint64_t seed)
{
    WorldDesc base;
    base.seed = seed;
    base.sizeMetres = 2048;
    base.metresPerSample = 8;
    base.cellMetres = 512;
    const WorldDesc desc = forgedWorldDesc(base);
    worldforge::ForgeConfig config;
    config.hydrology.riverThreshold = 48;
    worldforge::ForgedWorld world = worldforge::forgeWorld(desc.seed, kGrid, config, worldforge::ForgePhase::Hydrology);
    REQUIRE(world.ok());
    ForgedHeightfield forged = heightfieldFromForgedWorld(world, desc);
    REQUIRE(forged.ok());
    ForgedWater water = waterFromForgedWorld(world, forged.field);
    REQUIRE(water.ok());
    return Fixture{desc, std::move(world), std::move(forged.field), std::move(water)};
}

// How many of the forged cells that pass `pick` have water at the sample on top of them, and how many pass.
template <typename Pick>
std::pair<int, int> wetCells(const Fixture& f, const WaterField& waterField, Pick pick)
{
    const worldforge::HydrologyMap& map = *f.world.hydrology();
    int wet = 0;
    int seen = 0;
    for (int z = 1; z < kGrid - 1; ++z) {
        for (int x = 1; x < kGrid - 1; ++x) {
            const std::size_t cell = map.index(x, z);
            if (!pick(map, cell)) {
                continue;
            }
            ++seen;
            const double metres = f.desc.metresPerSample;
            if (waterField.at(x * kStride * metres, z * kStride * metres).wet()) {
                ++wet;
            }
        }
    }
    return {wet, seen};
}

}

TEST_CASE("a forged river cell has water and a dry cell does not")
{
    const Fixture f = makeFixture(23);
    const WaterField waterField(f.field, f.water.hydrology, f.water.rivers);

    const auto rivers = wetCells(f, waterField, [](const worldforge::HydrologyMap& m, std::size_t c) { return m.river[c] != 0 && m.lakeDepthM[c] == 0; });
    REQUIRE(rivers.second >= 10);
    CHECK(rivers.first == rivers.second);

    const auto lakes = wetCells(f, waterField, [](const worldforge::HydrologyMap& m, std::size_t c) { return m.lakeDepthM[c] != 0; });
    REQUIRE(lakes.second >= 1);
    CHECK(lakes.first * 2 >= lakes.second);

    const auto dry = wetCells(f, waterField, [&](const worldforge::HydrologyMap& m, std::size_t c) {
        if (m.river[c] != 0 || m.lakeDepthM[c] != 0 || m.heightM[c] <= kForgedSeaLevelM) {
            return false;
        }
        const int x = static_cast<int>(c % kGrid);
        const int z = static_cast<int>(c / kGrid);
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                const std::size_t near = m.index(std::clamp(x + dx, 0, kGrid - 1), std::clamp(z + dz, 0, kGrid - 1));
                if (m.river[near] != 0 || m.lakeDepthM[near] != 0) {
                    return false;
                }
            }
        }
        return true;
    });
    REQUIRE(dry.second >= 100);
    CHECK(dry.first == 0);
}

TEST_CASE("removing the feed leaves the river dry: the engine's own flood finds other water")
{
    const Fixture f = makeFixture(23);
    const auto standIn = std::make_shared<const Hydrology>(computeHydrology(f.field));
    const WaterField waterField(f.field, standIn, f.water.rivers);
    const auto rivers = wetCells(f, waterField, [](const worldforge::HydrologyMap& m, std::size_t c) { return m.river[c] != 0 && m.lakeDepthM[c] == 0; });
    REQUIRE(rivers.second >= 10);
    CHECK(rivers.first < rivers.second);
}

TEST_CASE("forged lakes stand at the forged level, flat, and over their ground")
{
    for (const std::uint64_t seed : {11ull, 23ull}) {
        const Fixture f = makeFixture(seed);
        const Hydrology& h = *f.water.hydrology;
        REQUIRE(h.side == f.desc.samplesPerSide());
        REQUIRE(h.lake.size() == h.filled.size());
        for (std::size_t i = 0; i < h.lake.size(); ++i) {
            const float ground = f.field.heightAtSample(static_cast<std::uint32_t>(i % h.side), static_cast<std::uint32_t>(i / h.side));
            REQUIRE(h.filled[i] >= ground);
            if (h.lake[i] != 0) {
                REQUIRE(h.filled[i] > ground);
                REQUIRE(h.lakeDeepest[h.lake[i] - 1] >= h.filled[i] - ground);
            } else {
                REQUIRE(h.filled[i] == ground);
            }
        }
    }
}

TEST_CASE("the forged water draws: a river cell's mesh has vertices and a dry cell's has none")
{
    const Fixture f = makeFixture(23);
    const worldforge::HydrologyMap& map = *f.world.hydrology();
    bool anyWet = false;
    bool anyDry = false;
    const std::uint32_t cellsPerSide = static_cast<std::uint32_t>(f.desc.sizeMetres / f.desc.cellMetres);
    for (std::uint32_t cz = 0; cz < cellsPerSide; ++cz) {
        for (std::uint32_t cx = 0; cx < cellsPerSide; ++cx) {
            const Mesh mesh = buildWaterMesh(f.field, *f.water.hydrology, f.water.rivers, CellId{static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cz)});
            (mesh.vertices.empty() ? anyDry : anyWet) = true;
        }
    }
    REQUIRE(!map.river.empty());
    CHECK(anyWet);
    CHECK(anyDry);
}

TEST_CASE("bad input comes back as an error and nothing throws")
{
    const Fixture f = makeFixture(23);
    CHECK(!waterFromForgedWorld(f.world, Heightfield()).ok());

    const worldforge::ForgedWorld early = worldforge::forgeWorld(23, kGrid, {}, worldforge::ForgePhase::Tectonics);
    CHECK(!waterFromForgedWorld(early, f.field).ok());

    const worldforge::ForgedWorld refused = worldforge::forgeWorld(23, 1, {}, worldforge::ForgePhase::Hydrology);
    CHECK(!waterFromForgedWorld(refused, f.field).ok());
}

TEST_CASE("the same forged world gives the same water")
{
    const Fixture a = makeFixture(23);
    const Fixture b = makeFixture(23);
    CHECK(a.water.hydrology->accumulation == b.water.hydrology->accumulation);
    CHECK(a.water.hydrology->lake == b.water.hydrology->lake);
    CHECK(a.water.hydrology->direction == b.water.hydrology->direction);
    CHECK(a.water.rivers.riverThreshold == b.water.rivers.riverThreshold);
}

TEST_CASE("the flow of forged water ends, whichever sample it starts from")
{
    const Fixture f = makeFixture(23);
    const Hydrology& h = *f.water.hydrology;
    const std::size_t total = h.filled.size();
    for (std::size_t start = 0; start < total; ++start) {
        std::size_t at = start;
        std::size_t steps = 0;
        while (!h.isOutlet(at) && steps <= total) {
            at = h.downstream(at);
            ++steps;
        }
        REQUIRE(steps <= total);
    }
    for (const std::uint32_t acc : h.accumulation) {
        REQUIRE(acc >= 1);
    }
}
