#include "ichor/world/forge.h"
#include "ichor/world/forged_biome.h"
#include "ichor/world/forged_heightfield.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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

// Forest on the west half, desert on the east half, one column of marsh in between to blend.
worldforge::BiomeMap halvesMap()
{
    worldforge::BiomeMap map;
    map.width = kGrid;
    map.height = kGrid;
    const auto cells = static_cast<std::size_t>(kGrid * kGrid);
    map.biome.assign(cells, static_cast<std::uint8_t>(BiomeId::Forest));
    map.secondary.assign(cells, static_cast<std::uint8_t>(BiomeId::Forest));
    map.blend.assign(cells, 0);
    for (int z = 0; z < kGrid; ++z) {
        for (int x = kGrid / 2; x < kGrid; ++x) {
            map.biome[map.index(x, z)] = static_cast<std::uint8_t>(BiomeId::Desert);
            map.secondary[map.index(x, z)] = static_cast<std::uint8_t>(BiomeId::Desert);
        }
    }
    return map;
}

std::size_t countKind(const std::vector<VegetationInstance>& plants, VegetationKind kind)
{
    return static_cast<std::size_t>(std::count_if(plants.begin(), plants.end(), [&](const VegetationInstance& p) { return p.kind == static_cast<std::uint32_t>(kind); }));
}

std::size_t trees(const std::vector<VegetationInstance>& plants)
{
    return plants.size() - countKind(plants, VegetationKind::Rock);
}

}

TEST_CASE("two cells in different biomes get different scatter")
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    const ForgedBiomeField biomes(halvesMap(), desc.sizeMetres);
    REQUIRE_FALSE(biomes.empty());
    REQUIRE(biomes.cellBiome(desc, {0, 1}) == BiomeId::Forest);
    REQUIRE(biomes.cellBiome(desc, {3, 1}) == BiomeId::Desert);

    std::vector<VegetationInstance> west;
    std::vector<VegetationInstance> east;
    scatterBiomeVegetation(field, biomes, {0, 1}, {}, west);
    scatterBiomeVegetation(field, biomes, {3, 1}, {}, east);

    REQUIRE(trees(west) > 20);
    REQUIRE(trees(east) == 0);
    REQUIRE(countKind(east, VegetationKind::Rock) > countKind(west, VegetationKind::Rock));
}

TEST_CASE("the biome lookup is what separates the cells")
{
    // The same two cells with the biome field swapped for a uniform one scatter alike in tree count,
    // so the difference above comes from the lookup and nothing else.
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    worldforge::BiomeMap uniform = halvesMap();
    std::fill(uniform.biome.begin(), uniform.biome.end(), static_cast<std::uint8_t>(BiomeId::Forest));
    std::fill(uniform.secondary.begin(), uniform.secondary.end(), static_cast<std::uint8_t>(BiomeId::Forest));
    const ForgedBiomeField biomes(uniform, desc.sizeMetres);

    std::vector<VegetationInstance> a;
    std::vector<VegetationInstance> b;
    scatterBiomeVegetation(field, biomes, {0, 1}, {}, a);
    scatterBiomeVegetation(field, biomes, {3, 1}, {}, b);
    REQUIRE(trees(a) > 20);
    REQUIRE(trees(b) > 20);
}

TEST_CASE("a cell scatters the same whatever was asked for before it")
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    const ForgedBiomeField biomes(halvesMap(), desc.sizeMetres);

    std::vector<VegetationInstance> first;
    std::vector<VegetationInstance> other;
    std::vector<VegetationInstance> again;
    scatterBiomeVegetation(field, biomes, {1, 2}, {}, first);
    scatterBiomeVegetation(field, biomes, {3, 0}, {}, other);
    scatterBiomeVegetation(field, biomes, {1, 2}, {}, again);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.size() == again.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].position == again[i].position);
        REQUIRE(first[i].kind == again[i].kind);
    }
}

TEST_CASE("the default forest density leaves the stand-in scatter unchanged")
{
    const WorldDesc desc = smallWorld();
    const Heightfield field = forgeHeightfield(desc);
    VegetationParams params;
    REQUIRE(params.forestDensity == 1.0f);
    std::vector<VegetationInstance> plain;
    scatterVegetation(field, {1, 1}, params, plain);
    params.forestDensity = 0.0f;
    std::vector<VegetationInstance> bare;
    scatterVegetation(field, {1, 1}, params, bare);
    REQUIRE(trees(plain) > 20);
    REQUIRE(trees(bare) < trees(plain) / 10);
}

TEST_CASE("a cell is judged by its land, not the sea at its edge")
{
    WorldDesc desc = smallWorld();
    worldforge::BiomeMap map = halvesMap();
    // Make the whole first cell column ocean except a few grassland cells.
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 0; x <= 8; ++x) map.biome[map.index(x, z)] = static_cast<std::uint8_t>(BiomeId::Ocean);
    }
    map.biome[map.index(2, 2)] = static_cast<std::uint8_t>(BiomeId::Grassland);
    const ForgedBiomeField biomes(map, desc.sizeMetres);
    REQUIRE(biomes.cellBiome(desc, {0, 0}) == BiomeId::Grassland);
    REQUIRE(biomes.cellBiome(desc, {0, 1}) == BiomeId::Ocean);
}

TEST_CASE("splat weights and colour blend across a border")
{
    const WorldDesc desc = smallWorld();
    worldforge::BiomeMap map = halvesMap();
    const std::size_t border = map.index(kGrid / 2, 4);
    map.secondary[border] = static_cast<std::uint8_t>(BiomeId::Forest);
    map.blend[border] = 128;
    const ForgedBiomeField biomes(map, desc.sizeMetres);

    const double step = static_cast<double>(desc.sizeMetres) / (kGrid - 1);
    const glm::vec3 inside = biomes.splatAt(2 * step, 4 * step);
    const glm::vec3 edge = biomes.splatAt((kGrid / 2) * step, 4 * step);
    const glm::vec3 desert = biomes.splatAt((kGrid - 2) * step, 4 * step);
    REQUIRE(inside == biomeSurface(BiomeId::Forest).splat);
    REQUIRE(desert == biomeSurface(BiomeId::Desert).splat);
    REQUIRE(edge.y > biomeSurface(BiomeId::Forest).splat.y);
    REQUIRE(edge.y < biomeSurface(BiomeId::Desert).splat.y);
    REQUIRE(biomes.groundColourAt((kGrid / 2) * step, 4 * step) != biomes.groundColourAt(2 * step, 4 * step));
}

TEST_CASE("every biome has a surface whose splat weights sum to one")
{
    for (int b = 0; b < worldforge::kBiomeCount; ++b) {
        const BiomeSurface& s = biomeSurface(static_cast<BiomeId>(b));
        REQUIRE(std::abs(s.splat.x + s.splat.y + s.splat.z - 1.0f) < 1e-5f);
        REQUIRE(s.forestDensity >= 0.0f);
        REQUIRE(s.forestDensity <= 1.0f);
        REQUIRE(s.rockChance >= 0.0f);
    }
    REQUIRE(&biomeSurface(static_cast<BiomeId>(200)) == &biomeSurface(BiomeId::Crag));
}

TEST_CASE("the snow line sits above the desc's sea")
{
    WorldDesc desc = smallWorld();
    desc.waterHeight = 100.0f;
    const VegetationParams alpine = vegetationParamsFor(BiomeId::Alpine, desc);
    const VegetationParams forest = vegetationParamsFor(BiomeId::Forest, desc);
    REQUIRE(alpine.maxHeight == 100.0f + 1400.0f);
    REQUIRE(alpine.minHeight == 100.0f + VegetationParams{}.minHeight);
    REQUIRE(alpine.maxHeight < forest.maxHeight);
    REQUIRE(alpine.spacing == VegetationParams{}.spacing);
}

TEST_CASE("hostile biome maps and positions never crash")
{
    const WorldDesc desc = smallWorld();
    worldforge::BiomeMap bad;
    REQUIRE(ForgedBiomeField(bad, desc.sizeMetres).empty());
    bad = halvesMap();
    bad.blend.pop_back();
    REQUIRE(ForgedBiomeField(bad, desc.sizeMetres).empty());
    bad = halvesMap();
    bad.height = kGrid - 1;
    REQUIRE(ForgedBiomeField(bad, desc.sizeMetres).empty());
    REQUIRE(ForgedBiomeField(halvesMap(), 0).empty());

    worldforge::BiomeMap wild = halvesMap();
    std::fill(wild.biome.begin(), wild.biome.end(), std::uint8_t{250});
    std::fill(wild.secondary.begin(), wild.secondary.end(), std::uint8_t{251});
    const ForgedBiomeField biomes(wild, desc.sizeMetres);
    REQUIRE(biomes.biomeAt(0, 0) == BiomeId::Crag);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE(biomes.biomeAt(nan, inf) == BiomeId::Crag);
    REQUIRE(biomes.biomeAt(-inf, 1e300) == BiomeId::Crag);
    const ForgedBiomeField empty;
    REQUIRE(empty.biomeAt(1, 1) == BiomeId::Crag);
    REQUIRE(empty.cellBiome(desc, {0, 0}) == BiomeId::Crag);
    REQUIRE(biomes.cellBiome(desc, {-5, 1 << 30}) == BiomeId::Crag);

    std::vector<VegetationInstance> out;
    scatterBiomeVegetation(forgeHeightfield(desc), biomes, {0, 0}, {}, out);
}

TEST_CASE("a real forged world gives every cell a biome")
{
    const WorldDesc desc = forgedWorldDesc(smallWorld());
    const worldforge::ForgedWorld world = worldforge::forgeWorld(desc.seed, 65, {}, worldforge::ForgePhase::Biomes);
    REQUIRE(world.ok());
    const ForgedBiomes biomes = biomesFromForgedWorld(world, desc);
    REQUIRE(biomes.ok());
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            REQUIRE(static_cast<int>(biomes.field.cellBiome(desc, {x, z})) < worldforge::kBiomeCount);
        }
    }

    const worldforge::ForgedWorld early = worldforge::forgeWorld(desc.seed, 65, {}, worldforge::ForgePhase::Hydrology);
    REQUIRE_FALSE(biomesFromForgedWorld(early, desc).ok());
    WorldDesc invalid = desc;
    invalid.cellMetres = 0;
    REQUIRE_FALSE(biomesFromForgedWorld(world, invalid).ok());
}

TEST_CASE("the blend at a position is the two biomes and the byte the splat uses")
{
    const WorldDesc desc = smallWorld();
    worldforge::BiomeMap map = halvesMap();
    map.secondary[map.index(2, 2)] = static_cast<std::uint8_t>(BiomeId::Marsh);
    map.blend[map.index(2, 2)] = 255;
    const ForgedBiomeField biomes(map, desc.sizeMetres);

    const double step = static_cast<double>(desc.sizeMetres) / (kGrid - 1);
    const BiomeBlend marsh = biomes.blendAt(2 * step, 2 * step);
    REQUIRE(marsh.primary == BiomeId::Forest);
    REQUIRE(marsh.secondary == BiomeId::Marsh);
    REQUIRE(marsh.weight == 1.0f);
    REQUIRE(biomes.blendAt(0.0, 0.0).weight == 0.0f);
    REQUIRE(biomes.blendAt(desc.sizeMetres, 0.0).primary == BiomeId::Desert);

    const BiomeBlend none = ForgedBiomeField{}.blendAt(10.0, 10.0);
    REQUIRE(none.primary == BiomeId::Crag);
    REQUIRE(none.weight == 0.0f);
}

TEST_CASE("the smooth splat changes gradually across a border where the nearest one jumps")
{
    const WorldDesc desc = smallWorld();
    const ForgedBiomeField biomes(halvesMap(), desc.sizeMetres);
    const double step = static_cast<double>(desc.sizeMetres) / (kGrid - 1);
    float smoothJump = 0.0f;
    float nearestJump = 0.0f;
    glm::vec3 last = biomes.smoothSplatAt(0.0, 4 * step);
    glm::vec3 lastNearest = biomes.splatAt(0.0, 4 * step);
    for (double x = 1.0; x <= desc.sizeMetres; x += 1.0) {
        const glm::vec3 now = biomes.smoothSplatAt(x, 4 * step);
        const glm::vec3 nearest = biomes.splatAt(x, 4 * step);
        smoothJump = std::max(smoothJump, std::abs(now.y - last.y));
        nearestJump = std::max(nearestJump, std::abs(nearest.y - lastNearest.y));
        REQUIRE(std::abs(now.x + now.y + now.z - 1.0f) < 1e-4f);
        last = now;
        lastNearest = nearest;
    }
    REQUIRE(nearestJump > 0.5f);
    REQUIRE(smoothJump < 0.05f);
    REQUIRE(biomes.smoothSplatAt(2 * step, 4 * step) == biomeSurface(BiomeId::Forest).splat);
    REQUIRE(biomes.smoothSplatAt((kGrid - 2) * step, 4 * step) == biomeSurface(BiomeId::Desert).splat);
    REQUIRE(ForgedBiomeField{}.smoothSplatAt(1.0, 1.0) == biomeSurface(BiomeId::Crag).splat);
    REQUIRE(biomes.smoothSplatAt(std::numeric_limits<double>::quiet_NaN(), -5.0) == biomes.smoothSplatAt(0.0, 0.0));
}
