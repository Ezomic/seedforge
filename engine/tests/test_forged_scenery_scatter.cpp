#include "ichor/world/forge.h"
#include "ichor/world/forged_scenery_scatter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>

using namespace ichor;
using worldforge::BiomeId;

namespace {

constexpr int kGrid = 33;

WorldDesc smallWorld()
{
    WorldDesc desc;
    desc.seed = 5;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return desc;
}

ForgedBiomeField uniform(BiomeId biome, std::uint32_t sizeMetres)
{
    worldforge::BiomeMap map;
    map.width = kGrid;
    map.height = kGrid;
    const auto cells = static_cast<std::size_t>(kGrid * kGrid);
    map.biome.assign(cells, static_cast<std::uint8_t>(biome));
    map.secondary.assign(cells, static_cast<std::uint8_t>(biome));
    map.blend.assign(cells, 0);
    return ForgedBiomeField(map, sizeMetres);
}

// A creek along x in [1100, 1140), inside cell {2, 1}: wet, so blocked, with a dry shore beside it.
SceneryGuard creek()
{
    SceneryGuard guard;
    guard.wet = [](double x, double) { return x >= 1100.0 && x < 1140.0; };
    guard.blocked = guard.wet;
    return guard;
}

std::set<std::uint32_t> kindsIn(const std::vector<VegetationInstance>& plants)
{
    std::set<std::uint32_t> kinds;
    for (const VegetationInstance& p : plants) kinds.insert(p.kind);
    return kinds;
}

bool has(const std::set<std::uint32_t>& kinds, VegetationKind kind)
{
    return kinds.contains(static_cast<std::uint32_t>(kind));
}

std::vector<VegetationInstance> scatter(BiomeId biome, const SceneryGuard& guard = {}, CellId cell = {2, 1})
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    const ForgedBiomeField biomes = uniform(biome, desc.sizeMetres);
    std::vector<VegetationInstance> out;
    scatterForgedScenery(field, biomes, guard, cell, {}, out);
    return out;
}

}

TEST_CASE("the biome scenery is planted the same way every time, whatever else was asked for first")
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    const ForgedBiomeField biomes = uniform(BiomeId::Forest, desc.sizeMetres);
    std::vector<VegetationInstance> first;
    std::vector<VegetationInstance> other;
    std::vector<VegetationInstance> again;
    scatterForgedScenery(field, biomes, {}, {1, 1}, {}, first);
    scatterForgedScenery(field, biomes, {}, {2, 2}, {}, other);
    scatterForgedScenery(field, biomes, {}, {1, 1}, {}, again);
    REQUIRE(first.size() > 100);
    REQUIRE(first.size() == again.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].position == again[i].position);
        REQUIRE(first[i].yaw == again[i].yaw);
        REQUIRE(first[i].scale == again[i].scale);
        REQUIRE(first[i].kind == again[i].kind);
    }
}

TEST_CASE("each biome grows its own species")
{
    const auto forest = kindsIn(scatter(BiomeId::Forest));
    const auto desert = kindsIn(scatter(BiomeId::Desert));
    const auto tundra = kindsIn(scatter(BiomeId::Tundra));
    const auto marsh = kindsIn(scatter(BiomeId::Marsh, creek()));
    const auto taiga = kindsIn(scatter(BiomeId::Taiga));

    REQUIRE(has(forest, VegetationKind::Grass));
    REQUIRE(has(forest, VegetationKind::Bush));
    REQUIRE(has(forest, VegetationKind::Conifer));
    REQUIRE(has(forest, VegetationKind::Broadleaf));
    REQUIRE_FALSE(has(forest, VegetationKind::Cactus));

    REQUIRE(has(desert, VegetationKind::Cactus));
    REQUIRE(has(desert, VegetationKind::DryShrub));
    REQUIRE_FALSE(has(desert, VegetationKind::Grass));
    REQUIRE_FALSE(has(desert, VegetationKind::Conifer));
    REQUIRE_FALSE(has(desert, VegetationKind::Broadleaf));

    REQUIRE(has(tundra, VegetationKind::DeadTree));
    REQUIRE_FALSE(has(tundra, VegetationKind::Cactus));

    REQUIRE(has(marsh, VegetationKind::Reed));
    REQUIRE_FALSE(has(forest, VegetationKind::Reed));

    REQUIRE(has(taiga, VegetationKind::Conifer));
    REQUIRE(forest != desert);
    REQUIRE(forest != tundra);
    REQUIRE(desert != tundra);
    REQUIRE(forest != marsh);
}

TEST_CASE("a forest is mostly broadleaf and a taiga mostly conifer")
{
    const auto share = [](const std::vector<VegetationInstance>& plants, VegetationKind kind) {
        const auto trees = std::count_if(plants.begin(), plants.end(), [](const VegetationInstance& p) {
            return p.kind == static_cast<std::uint32_t>(VegetationKind::Conifer) || p.kind == static_cast<std::uint32_t>(VegetationKind::Broadleaf);
        });
        const auto wanted = std::count_if(plants.begin(), plants.end(), [&](const VegetationInstance& p) { return p.kind == static_cast<std::uint32_t>(kind); });
        return trees > 0 ? static_cast<double>(wanted) / static_cast<double>(trees) : 0.0;
    };
    const auto forest = scatter(BiomeId::Forest);
    const auto taiga = scatter(BiomeId::Taiga);
    REQUIRE(share(forest, VegetationKind::Conifer) < 0.5);
    REQUIRE(share(taiga, VegetationKind::Conifer) > 0.8);
}

TEST_CASE("nothing grows where the guard says it may not")
{
    const SceneryGuard guard = creek();
    const WorldDesc desc = smallWorld();
    const double originX = static_cast<double>(desc.cellMetres) * 2;
    const auto inCreek = [&](const VegetationInstance& p) {
        const double x = originX + p.position.x;
        return x >= 1100.0 && x < 1140.0;
    };

    // Without the guard the creek's strip is as full as any other; with it, it is bare.
    const auto open = scatter(BiomeId::Forest);
    const auto guarded = scatter(BiomeId::Forest, guard);
    REQUIRE(std::count_if(open.begin(), open.end(), inCreek) > 20);
    REQUIRE(std::count_if(guarded.begin(), guarded.end(), inCreek) == 0);
    REQUIRE(guarded.size() > open.size() / 2);
}

TEST_CASE("reeds stand on the shore and not in the water")
{
    const auto marsh = scatter(BiomeId::Marsh, creek());
    const double originX = 1024.0;
    std::size_t reeds = 0;
    for (const VegetationInstance& p : marsh) {
        if (p.kind != static_cast<std::uint32_t>(VegetationKind::Reed)) continue;
        ++reeds;
        const double x = originX + p.position.x;
        const double toWater = std::min(std::abs(x - 1100.0), std::abs(x - 1140.0));
        REQUIRE(toWater <= kReedShoreM);
    }
    REQUIRE(reeds > 3);
    REQUIRE(kindsIn(scatter(BiomeId::Marsh)).contains(static_cast<std::uint32_t>(VegetationKind::Reed)) == false);
}

TEST_CASE("water biomes grow no plants, only the odd rock")
{
    for (const BiomeId biome : {BiomeId::Ocean, BiomeId::Lake}) {
        for (const VegetationInstance& p : scatter(biome)) {
            REQUIRE(p.kind == static_cast<std::uint32_t>(VegetationKind::Rock));
        }
    }
}

TEST_CASE("an empty biome field scatters without crashing")
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    std::vector<VegetationInstance> out;
    scatterForgedScenery(field, ForgedBiomeField{}, {}, {1, 1}, {}, out);
    for (const VegetationInstance& p : out) {
        REQUIRE(std::isfinite(p.position.y));
        REQUIRE(p.kind < static_cast<std::uint32_t>(VegetationKind::Count));
    }
}

TEST_CASE("every kind has a mesh")
{
    REQUIRE(treeBundle().meshes.size() == static_cast<std::size_t>(VegetationKind::Count));
}
