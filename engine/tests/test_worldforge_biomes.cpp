#include <worldforge/biome_map.h>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <limits>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;
constexpr int kW = 64;
constexpr int kH = 64;

struct World {
    ClimateMap climate;
    HydrologyMap hydro;

    BiomeInputs inputs() const { return { &climate, &hydro }; }
};

// Flat land at `heightM` with uniform temperature and moisture and no water.
World makeFlat(int heightM, int tempC10, int moisture, int w = kW, int h = kH)
{
    World world;
    const std::size_t cells = static_cast<std::size_t>(w) * h;
    world.climate.seed = 1;
    world.climate.width = w;
    world.climate.height = h;
    world.climate.temperatureC10.assign(cells, static_cast<std::int16_t>(tempC10));
    world.climate.moisture.assign(cells, static_cast<std::uint16_t>(moisture));
    world.hydro.seed = 1;
    world.hydro.width = w;
    world.hydro.height = h;
    world.hydro.filledM.assign(cells, static_cast<std::int16_t>(heightM));
    world.hydro.heightM.assign(cells, static_cast<std::int16_t>(heightM));
    world.hydro.lakeDepthM.assign(cells, 0);
    world.hydro.flowDir.assign(cells, kHydrologyNoFlow);
    world.hydro.accumulation.assign(cells, 1);
    world.hydro.river.assign(cells, 0);
    return world;
}

BiomeMap biomes(const World& world, const BiomeParams& params = {})
{
    auto out = generateBiomes(world.inputs(), params);
    REQUIRE(out.has_value());
    return *out;
}

BiomeId centre(const BiomeMap& m) { return static_cast<BiomeId>(m.biome[m.index(kW / 2, kH / 2)]); }

int count(const BiomeMap& m, BiomeId id)
{
    int n = 0;
    for (const std::uint8_t v : m.biome) n += v == static_cast<std::uint8_t>(id);
    return n;
}

BiomeId lookup(int tempC10, int moisture, int heightM = 100)
{
    return centre(biomes(makeFlat(heightM, tempC10, moisture)));
}

struct Pipeline {
    ErodedMap eroded;
    HydrologyMap hydro;
    ClimateMap climate;

    BiomeInputs inputs() const { return { &climate, &hydro }; }
};

Pipeline makePipeline(std::uint64_t seed, int size)
{
    auto tectonics = generateTectonics(seed, { size, size, 12 });
    REQUIRE(tectonics.has_value());
    auto eroded = erodeTectonics(*tectonics);
    REQUIRE(eroded.has_value());
    auto hydro = computeHydrology(*eroded);
    REQUIRE(hydro.has_value());
    auto climate = generateClimate(*eroded);
    REQUIRE(climate.has_value());
    return { *eroded, *hydro, *climate };
}

}

TEST_CASE("biome lookup follows the temperature by moisture table", "[worldforge][biomes]")
{
    CHECK(lookup(-400, 8) == BiomeId::Glacier);
    CHECK(lookup(-100, 100) == BiomeId::Tundra);
    CHECK(lookup(50, 0) == BiomeId::Tundra);
    CHECK(lookup(50, 8) == BiomeId::Taiga);
    CHECK(lookup(150, 0, 700) == BiomeId::Grassland);
    CHECK(lookup(150, 8) == BiomeId::Forest);
    CHECK(lookup(230, 0) == BiomeId::Desert);
    CHECK(lookup(230, 100) == BiomeId::Forest);
    CHECK(lookup(300, 0) == BiomeId::Desert);
    CHECK(lookup(300, 8) == BiomeId::Savanna);
    CHECK(lookup(300, 100) == BiomeId::Rainforest);
}

TEST_CASE("biome lookup reacts to temperature and moisture alone", "[worldforge][biomes]")
{
    // Removing the lookup would make every cell the same biome.
    std::uint32_t seen = 0;
    for (int t = -500; t <= 500; t += 50) {
        for (int m = 0; m <= 40; m += 2) seen |= biomeBit(lookup(t, m, 700));
    }
    CHECK(std::popcount(seen) >= 7);
}

TEST_CASE("biome water cells follow the hydrology map", "[worldforge][biomes]")
{
    World world = makeFlat(100, 200, 8);
    const std::size_t lake = world.hydro.index(10, 10);
    const std::size_t sea = world.hydro.index(20, 20);
    world.hydro.lakeDepthM[lake] = 5;
    world.hydro.heightM[sea] = -40;
    const BiomeMap out = biomes(world);
    CHECK(out.biome[lake] == static_cast<std::uint8_t>(BiomeId::Lake));
    CHECK(out.biome[sea] == static_cast<std::uint8_t>(BiomeId::Ocean));
    CHECK(count(out, BiomeId::Lake) == 1);
    CHECK(count(out, BiomeId::Ocean) == 1);

    BiomeParams high;
    high.seaLevelM = 100;
    const BiomeMap flooded = biomes(world, high);
    CHECK(count(flooded, BiomeId::Ocean) == kW * kH - 1);
}

TEST_CASE("biome slope and elevation modulate the table", "[worldforge][biomes]")
{
    World world = makeFlat(300, 150, 8);
    const std::size_t spike = world.hydro.index(30, 30);
    world.hydro.heightM[spike] = 700;
    BiomeMap out = biomes(world);
    CHECK(out.biome[spike] == static_cast<std::uint8_t>(BiomeId::Crag));
    CHECK(out.biome[world.hydro.index(31, 30)] == static_cast<std::uint8_t>(BiomeId::Crag));
    CHECK(out.biome[world.hydro.index(5, 5)] == static_cast<std::uint8_t>(BiomeId::Forest));

    World high = makeFlat(2600, 150, 8);
    CHECK(centre(biomes(high)) == BiomeId::Alpine);
    World glacial = makeFlat(2600, -400, 8);
    CHECK(centre(biomes(glacial)) == BiomeId::Glacier);

    BiomeParams noCrag;
    noCrag.cragSlopeM = kBiomeMaxSlopeM;
    CHECK(biomes(world, noCrag).biome[spike] != static_cast<std::uint8_t>(BiomeId::Crag));
}

TEST_CASE("biome lowland grassland becomes farmland, steep or high ground does not", "[worldforge][biomes]")
{
    CHECK(lookup(150, 0, 200) == BiomeId::LowlandFarm);
    CHECK(lookup(150, 0, 900) == BiomeId::Grassland);
    World world = makeFlat(200, 150, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) world.hydro.heightM[world.hydro.index(x, y)] = static_cast<std::int16_t>(200 + x * 40);
    }
    CHECK(count(biomes(world), BiomeId::LowlandFarm) == 0);
}

TEST_CASE("biome marsh needs wet flat lowland beside water", "[worldforge][biomes]")
{
    World world = makeFlat(20, 150, 100);
    world.hydro.river[world.hydro.index(30, 30)] = 1;
    const BiomeMap out = biomes(world);
    CHECK(out.biome[world.hydro.index(31, 31)] == static_cast<std::uint8_t>(BiomeId::Marsh));
    CHECK(out.biome[world.hydro.index(30, 30)] == static_cast<std::uint8_t>(BiomeId::Marsh));
    CHECK(out.biome[world.hydro.index(40, 40)] == static_cast<std::uint8_t>(BiomeId::Forest));
    CHECK(count(out, BiomeId::Marsh) == 9);

    World dry = makeFlat(20, 150, 0);
    dry.hydro.river[dry.hydro.index(30, 30)] = 1;
    CHECK(count(biomes(dry), BiomeId::Marsh) == 0);
}

TEST_CASE("biome injection covers the eligible cells it is told to", "[worldforge][biomes]")
{
    const World world = makeFlat(100, 300, 8);
    BiomeParams all;
    all.injections.push_back({ BiomeId::SaltBlight, 256, -6000, 8000, 0, 4 });
    CHECK(count(biomes(world, all), BiomeId::SaltBlight) == kW * kH);

    BiomeParams none;
    none.injections.push_back({ BiomeId::SaltBlight, 0, -6000, 8000, 0, 4 });
    CHECK(count(biomes(world, none), BiomeId::SaltBlight) == 0);

    BiomeParams half;
    half.injections.push_back({ BiomeId::SaltBlight, 128, -6000, 8000, 0, 1 });
    const int n = count(biomes(world, half), BiomeId::SaltBlight);
    CHECK(n > kW * kH * 4 / 10);
    CHECK(n < kW * kH * 6 / 10);

    BiomeParams band;
    band.injections.push_back({ BiomeId::TidalRuin, 256, 500, 900, 0, 4 });
    CHECK(count(biomes(world, band), BiomeId::TidalRuin) == 0);
}

TEST_CASE("biome injection with a coast reach stays near the sea", "[worldforge][biomes]")
{
    World world = makeFlat(100, 300, 8);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < 8; ++x) world.hydro.heightM[world.hydro.index(x, y)] = -50;
    }
    BiomeParams p;
    p.injections.push_back({ BiomeId::TidalRuin, 256, -6000, 8000, 5, 1 });
    const BiomeMap out = biomes(world, p);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool injected = out.biome[out.index(x, y)] == static_cast<std::uint8_t>(BiomeId::TidalRuin);
            CHECK(injected == (x >= 8 && x < 8 + 5));
        }
    }
    CHECK(count(out, BiomeId::Ocean) == 8 * kH);
}

TEST_CASE("biome shores are not cliffs", "[worldforge][biomes]")
{
    World world = makeFlat(100, 300, 8);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < 8; ++x) world.hydro.heightM[world.hydro.index(x, y)] = -300;
    }
    world.hydro.lakeDepthM[world.hydro.index(40, 40)] = 200;
    world.hydro.heightM[world.hydro.index(40, 40)] = -100;
    const BiomeMap out = biomes(world);
    CHECK(count(out, BiomeId::Crag) == 0);
    CHECK(out.biome[out.index(8, 20)] == static_cast<std::uint8_t>(BiomeId::Savanna));
    CHECK(out.biome[out.index(41, 40)] == static_cast<std::uint8_t>(BiomeId::Savanna));
}

TEST_CASE("biome later injections overwrite earlier ones", "[worldforge][biomes]")
{
    const World world = makeFlat(100, 300, 8);
    BiomeParams p;
    p.injections.push_back({ BiomeId::SaltBlight, 256, -6000, 8000, 0, 4 });
    p.injections.push_back({ BiomeId::TidalRuin, 256, -6000, 8000, 0, 4 });
    CHECK(count(biomes(world, p), BiomeId::TidalRuin) == kW * kH);
}

TEST_CASE("biome suppression removes a biome and falls back down the chain", "[worldforge][biomes]")
{
    const World farmland = makeFlat(100, 150, 0);
    CHECK(centre(biomes(farmland)) == BiomeId::LowlandFarm);

    BiomeParams noFarm;
    noFarm.suppressMask = biomeBit(BiomeId::LowlandFarm);
    CHECK(centre(biomes(farmland, noFarm)) == BiomeId::Grassland);

    noFarm.suppressMask |= biomeBit(BiomeId::Grassland) | biomeBit(BiomeId::Forest);
    CHECK(centre(biomes(farmland, noFarm)) == BiomeId::Taiga);

    BiomeParams everything;
    everything.suppressMask = kBiomeSuppressible;
    const BiomeMap out = biomes(farmland, everything);
    CHECK(count(out, BiomeId::Crag) == kW * kH);
}

TEST_CASE("biome suppression beats injection of the same biome", "[worldforge][biomes]")
{
    const World world = makeFlat(100, 300, 8);
    BiomeParams p;
    p.suppressMask = biomeBit(BiomeId::SaltBlight);
    p.injections.push_back({ BiomeId::SaltBlight, 256, -6000, 8000, 0, 4 });
    const BiomeMap out = biomes(world, p);
    CHECK(count(out, BiomeId::SaltBlight) == 0);
    CHECK(count(out, BiomeId::Savanna) == kW * kH);
}

TEST_CASE("biome suppressed biomes never appear on a generated world", "[worldforge][biomes]")
{
    const Pipeline pipe = makePipeline(kSeed, 128);
    const BiomeMap base = *generateBiomes(pipe.inputs());
    int suppressed = 0;
    for (int id = 0; id < kBiomeCount; ++id) {
        if ((kBiomeSuppressible & (1u << id)) == 0 || count(base, static_cast<BiomeId>(id)) == 0) continue;
        BiomeParams p;
        p.suppressMask = 1u << id;
        const BiomeMap out = *generateBiomes(pipe.inputs(), p);
        CHECK(count(out, static_cast<BiomeId>(id)) == 0);
        CHECK(count(out, BiomeId::Ocean) == count(base, BiomeId::Ocean));
        ++suppressed;
    }
    CHECK(suppressed >= 4);
}

TEST_CASE("biome blend marks region borders and nothing else", "[worldforge][biomes]")
{
    World world = makeFlat(100, 300, 8);
    for (int y = 0; y < kH; ++y) {
        for (int x = kW / 2; x < kW; ++x) world.climate.moisture[world.climate.index(x, y)] = 100;
    }
    const BiomeMap out = biomes(world);
    const std::size_t left = out.index(kW / 2 - 1, 20);
    const std::size_t right = out.index(kW / 2, 20);
    CHECK(out.biome[left] == static_cast<std::uint8_t>(BiomeId::Savanna));
    CHECK(out.biome[right] == static_cast<std::uint8_t>(BiomeId::Rainforest));
    CHECK(out.blend[left] == 255 / 4);
    CHECK(out.secondary[left] == static_cast<std::uint8_t>(BiomeId::Rainforest));
    CHECK(out.secondary[right] == static_cast<std::uint8_t>(BiomeId::Savanna));
    CHECK(out.blend[out.index(5, 20)] == 0);
    CHECK(out.secondary[out.index(5, 20)] == static_cast<std::uint8_t>(BiomeId::Savanna));

    std::int64_t borderSum = 0;
    std::int64_t interiorSum = 0;
    for (int y = 1; y < kH - 1; ++y) {
        for (int x = 1; x < kW - 1; ++x) {
            const bool border = x == kW / 2 - 1 || x == kW / 2;
            (border ? borderSum : interiorSum) += out.blend[out.index(x, y)];
        }
    }
    CHECK(borderSum / (2 * (kH - 2)) > 50);
    CHECK(interiorSum == 0);
}

TEST_CASE("biome golden hash is stable", "[worldforge][biomes]")
{
    const Pipeline pipe = makePipeline(kSeed, 128);
    BiomeParams p;
    p.injections.push_back({ BiomeId::SaltBlight, 96, 0, 400, 6, 4 });
    p.suppressMask = biomeBit(BiomeId::LowlandFarm);
    const BiomeMap out = *generateBiomes(pipe.inputs(), p);
    CHECK(hashBiomeMap(out) == 0xb002edecf6393e6bull);
}

TEST_CASE("biome generation is deterministic and seed dependent", "[worldforge][biomes]")
{
    const Pipeline a = makePipeline(kSeed, 96);
    const Pipeline b = makePipeline(kSeed, 96);
    const Pipeline c = makePipeline(kSeed + 1, 96);
    CHECK(hashBiomeMap(*generateBiomes(a.inputs())) == hashBiomeMap(*generateBiomes(b.inputs())));
    CHECK(hashBiomeMap(*generateBiomes(a.inputs())) != hashBiomeMap(*generateBiomes(c.inputs())));
}

TEST_CASE("biome injection depends on the seed", "[worldforge][biomes]")
{
    World a = makeFlat(100, 300, 8);
    World b = a;
    b.climate.seed = 2;
    b.hydro.seed = 2;
    BiomeParams p;
    p.injections.push_back({ BiomeId::SaltBlight, 128, -6000, 8000, 0, 2 });
    CHECK(hashBiomeMap(biomes(a, p)) != hashBiomeMap(biomes(b, p)));
}

TEST_CASE("biome map on a generated world is climatically coherent", "[worldforge][biomes]")
{
    const Pipeline pipe = makePipeline(kSeed, 192);
    const BiomeMap out = *generateBiomes(pipe.inputs());
    REQUIRE(out.biome.size() == pipe.climate.temperatureC10.size());

    std::int64_t tempSum[kBiomeCount] = {};
    std::int64_t n[kBiomeCount] = {};
    for (std::size_t i = 0; i < out.biome.size(); ++i) {
        REQUIRE(out.biome[i] < kBiomeCount);
        tempSum[out.biome[i]] += pipe.climate.temperatureC10[i];
        ++n[out.biome[i]];
    }
    const auto id = [](BiomeId b) { return static_cast<std::size_t>(b); };
    REQUIRE(n[id(BiomeId::Ocean)] > 0);
    REQUIRE(n[id(BiomeId::Glacier)] > 0);
    REQUIRE(n[id(BiomeId::Tundra)] > 0);
    REQUIRE(n[id(BiomeId::Grassland)] > 0);
    const auto meanTemp = [&](BiomeId b) { return tempSum[id(b)] / n[id(b)]; };
    CHECK(meanTemp(BiomeId::Glacier) < meanTemp(BiomeId::Tundra));
    CHECK(meanTemp(BiomeId::Tundra) < meanTemp(BiomeId::Grassland) + 1);
    CHECK(meanTemp(BiomeId::Glacier) < -150);
    int distinct = 0;
    for (int b = 0; b < kBiomeCount; ++b) distinct += n[b] > 0;
    CHECK(distinct >= 6);
}

TEST_CASE("biome rejects malformed inputs", "[worldforge][biomes]")
{
    const World ok = makeFlat(100, 200, 8);
    CHECK(generateBiomes({ nullptr, &ok.hydro }) == std::nullopt);
    CHECK(generateBiomes({ &ok.climate, nullptr }) == std::nullopt);
    CHECK(generateBiomes({}) == std::nullopt);

    World bad = ok;
    bad.climate.moisture.pop_back();
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.climate.temperatureC10.clear();
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.river.pop_back();
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.lakeDepthM.clear();
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.heightM[3] = 9000;
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.heightM[3] = std::numeric_limits<std::int16_t>::min();
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.climate.moisture[3] = 65535;
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.seed = 99;
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    bad = ok;
    bad.hydro.width = kW / 2;
    CHECK(generateBiomes(bad.inputs()) == std::nullopt);
    CHECK(generateBiomes(makeFlat(0, 0, 0, 8, 8).inputs()) == std::nullopt);
    CHECK(generateBiomes(makeFlat(0, 0, 0, 2048, 16).inputs()) == std::nullopt);
}

TEST_CASE("biome rejects out of range parameters", "[worldforge][biomes]")
{
    const World world = makeFlat(100, 200, 8);
    const auto rejected = [&](auto edit) {
        BiomeParams p;
        edit(p);
        return !generateBiomes(world.inputs(), p).has_value();
    };
    CHECK(rejected([](BiomeParams& p) { p.seaLevelM = 9000; }));
    CHECK(rejected([](BiomeParams& p) { p.alpineM = -7000; }));
    CHECK(rejected([](BiomeParams& p) { p.cragSlopeM = 0; }));
    CHECK(rejected([](BiomeParams& p) { p.cragSlopeM = std::numeric_limits<int>::max(); }));
    CHECK(rejected([](BiomeParams& p) { p.marshMaxSlopeM = -1; }));
    CHECK(rejected([](BiomeParams& p) { p.farmMaxSlopeM = kBiomeMaxSlopeM + 1; }));
    CHECK(rejected([](BiomeParams& p) { p.tempEdgesC10[2] = p.tempEdgesC10[1] - 1; }));
    CHECK(rejected([](BiomeParams& p) { p.tempEdgesC10[4] = 601; }));
    CHECK(rejected([](BiomeParams& p) { p.moistureEdges[1] = 1; }));
    CHECK(rejected([](BiomeParams& p) { p.moistureEdges[2] = 1025; }));
    CHECK(rejected([](BiomeParams& p) { p.suppressMask = biomeBit(BiomeId::Ocean); }));
    CHECK(rejected([](BiomeParams& p) { p.suppressMask = biomeBit(BiomeId::Crag); }));
    CHECK(rejected([](BiomeParams& p) { p.suppressMask = 0xFFFFFFFFu; }));
    CHECK(rejected([](BiomeParams& p) { p.injections.assign(kBiomeMaxInjections + 1, BiomeInjection{}); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Ocean, 10, -10, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Lake, 10, -10, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ static_cast<BiomeId>(200), 10, -10, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, 257, -10, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, -1, -10, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, 10, 50, 10, 0, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, 10, -10, 10, 256, 1 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, 10, -10, 10, 0, 0 }); }));
    CHECK(rejected([](BiomeParams& p) { p.injections.push_back({ BiomeId::Marsh, 10, -10, 10, 0, 65 }); }));

    BiomeParams edge;
    edge.tempEdgesC10 = { -600, -600, 600, 600, 600 };
    edge.moistureEdges = { 0, 0, 1024 };
    edge.cragSlopeM = kBiomeMaxSlopeM;
    edge.injections.assign(kBiomeMaxInjections, BiomeInjection{ BiomeId::Alpine, 256, -6000, 8000, 255, 64 });
    CHECK(generateBiomes(world.inputs(), edge).has_value());
}

TEST_CASE("biome names map to ids", "[worldforge][biomes]")
{
    for (int i = 0; i < kBiomeCount; ++i) {
        const auto id = biomeFromName(kBiomeNames[static_cast<std::size_t>(i)]);
        REQUIRE(id.has_value());
        CHECK(static_cast<int>(*id) == i);
    }
    CHECK(biomeFromName("salt_blight") == BiomeId::SaltBlight);
    CHECK(biomeFromName("lowland_farm") == BiomeId::LowlandFarm);
    CHECK(biomeFromName("") == std::nullopt);
    CHECK(biomeFromName("Forest") == std::nullopt);
    CHECK(biomeFromName("nonsense") == std::nullopt);
}

TEST_CASE("biome handles the largest and smallest grids", "[worldforge][biomes]")
{
    const World small = makeFlat(100, 200, 8, kTectonicsMinSize, kTectonicsMinSize);
    CHECK(generateBiomes(small.inputs()).has_value());
    const World wide = makeFlat(100, 200, 8, kTectonicsMaxSize, kTectonicsMinSize);
    BiomeParams p;
    p.injections.push_back({ BiomeId::Marsh, 200, -6000, 8000, 255, 64 });
    CHECK(generateBiomes(wide.inputs(), p).has_value());
}
