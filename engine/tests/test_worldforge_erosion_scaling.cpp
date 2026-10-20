#include <worldforge/erosion_map.h>
#include <worldforge/forge_world.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using namespace worldforge;

namespace {

double landPercent(const std::vector<std::int16_t>& heights)
{
    const auto land = std::count_if(heights.begin(), heights.end(), [](std::int16_t h) { return h >= 0; });
    return 100.0 * static_cast<double>(land) / static_cast<double>(heights.size());
}

struct Shares {
    double tectonic = 0;
    double scaled = 0;
    double unscaled = 0;
    double scaledLoss() const { return tectonic - scaled; }
    double unscaledLoss() const { return tectonic - unscaled; }
};

Shares measure(std::uint64_t seed, int size)
{
    static std::map<std::pair<std::uint64_t, int>, Shares> cache;
    const auto key = std::make_pair(seed, size);
    if (const auto it = cache.find(key); it != cache.end()) return it->second;
    const ForgedWorld world = forgeWorld(seed, size, {}, ForgePhase::Erosion);
    REQUIRE(world.complete());
    Shares s{ landPercent(world.tectonics()->heightM), landPercent(world.eroded()->heightM), 0 };
    if (size >= kErosionReferenceSide) {
        s.unscaled = s.scaled;
    } else {
        const auto unscaled = erodeTectonics(*world.tectonics());
        REQUIRE(unscaled.has_value());
        s.unscaled = landPercent(unscaled->heightM);
    }
    cache.emplace(key, s);
    return s;
}

// Points of land share that erosion removes (negative: it adds), the part of the share that erosion
// controls. The share straight out of tectonics differs per size by itself, so it is not compared.
struct Spread {
    double scaled = 0;
    double unscaled = 0;
};

Spread worstLossSpread(const std::vector<int>& sizes)
{
    Spread worst;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        double scaledLo = 1e9, scaledHi = -1e9, unscaledLo = 1e9, unscaledHi = -1e9;
        for (const int size : sizes) {
            const Shares s = measure(seed, size);
            scaledLo = std::min(scaledLo, s.scaledLoss());
            scaledHi = std::max(scaledHi, s.scaledLoss());
            unscaledLo = std::min(unscaledLo, s.unscaledLoss());
            unscaledHi = std::max(unscaledHi, s.unscaledLoss());
        }
        worst.scaled = std::max(worst.scaled, scaledHi - scaledLo);
        worst.unscaled = std::max(worst.unscaled, unscaledHi - unscaledLo);
    }
    return worst;
}

constexpr double kLossSpreadTolerancePoints = 5.0;
// 1024 erodes slightly less than 256 (it adds a little land), so the span including it is wider.
constexpr double kLossSpreadTolerancePoints1024 = 6.0;

}

TEST_CASE("scaleErosionToGrid leaves the reference side and larger grids unchanged", "[worldforge][erosion_scaling]")
{
    const ErosionParams base;
    for (const int side : { 256, 257, 512, 1024, 4096 }) {
        const ErosionParams p = scaleErosionToGrid(base, side);
        CHECK(p.iterations == base.iterations);
        CHECK(p.thermalRate == base.thermalRate);
        CHECK(p.thermalPasses == base.thermalPasses);
        CHECK(p.strength == base.strength);
        CHECK(p.erodeRate == base.erodeRate);
        CHECK(p.depositRate == base.depositRate);
        CHECK(p.talusM == base.talusM);
    }
}

TEST_CASE("scaleErosionToGrid scales rounds and thermal rate with the side below the reference", "[worldforge][erosion_scaling]")
{
    const ErosionParams base;
    const ErosionParams at64 = scaleErosionToGrid(base, 64);
    CHECK(at64.iterations == 12);
    CHECK(at64.thermalRate == 32);
    const ErosionParams at128 = scaleErosionToGrid(base, 128);
    CHECK(at128.iterations == 24);
    CHECK(at128.thermalRate == 64);
    CHECK(at64.strength == base.strength);
    CHECK(at64.talusM == base.talusM);
}

TEST_CASE("scaleErosionToGrid keeps disabled terms disabled and never rounds an enabled one to zero", "[worldforge][erosion_scaling]")
{
    ErosionParams off;
    off.iterations = 0;
    off.thermalRate = 0;
    const ErosionParams scaledOff = scaleErosionToGrid(off, 64);
    CHECK(scaledOff.iterations == 0);
    CHECK(scaledOff.thermalRate == 0);

    ErosionParams one;
    one.iterations = 1;
    one.thermalRate = 1;
    for (const int side : { 1, 2, 16, 64, 255 }) {
        const ErosionParams p = scaleErosionToGrid(one, side);
        CHECK(p.iterations == 1);
        CHECK(p.thermalRate == 1);
    }
    CHECK(scaleErosionToGrid(ErosionParams{}, 0).iterations == 1);
    CHECK(scaleErosionToGrid(ErosionParams{}, -5).thermalRate == 1);
}

TEST_CASE("scaled params stay valid for erodeTectonics", "[worldforge][erosion_scaling]")
{
    const auto base = generateTectonics(1, { 64, 64, 12 });
    REQUIRE(base.has_value());
    ErosionParams maxed;
    maxed.iterations = kErosionMaxIterations;
    maxed.thermalPasses = 0;
    maxed.thermalRate = kErosionMaxRate;
    for (const int side : { 1, 63, 64, 255, 256 }) CHECK(erodeTectonics(*base, scaleErosionToGrid(maxed, side)).has_value());
}

TEST_CASE("the land erosion removes is about the same at 64, 128 and 256", "[worldforge][erosion_scaling]")
{
    const Spread spread = worstLossSpread({ 64, 128, 256 });
    INFO("worst per seed spread of land lost to erosion, in points: scaled " << spread.scaled << ", unscaled " << spread.unscaled);
    CHECK(spread.scaled < kLossSpreadTolerancePoints);
    // The metric must go red without the scaling, or the test above proves nothing.
    CHECK(spread.unscaled > 2 * kLossSpreadTolerancePoints);
}

TEST_CASE("thin continents survive erosion at size 64", "[worldforge][erosion_scaling]")
{
    for (const std::uint64_t seed : { 3ull, 4ull, 5ull }) {
        const Shares s = measure(seed, 64);
        INFO("seed " << seed << ": tectonic " << s.tectonic << "%, scaled " << s.scaled << "%, unscaled " << s.unscaled << "%");
        CHECK(s.scaled > 0.6 * s.tectonic);
        CHECK(s.unscaled < 0.2 * s.tectonic);
    }
}

TEST_CASE("the land erosion removes is about the same from 64 to 1024", "[.erosion_scaling_1024]")
{
    const Spread spread = worstLossSpread({ 64, 128, 256, 1024 });
    INFO("worst per seed spread of land lost to erosion, in points: scaled " << spread.scaled);
    CHECK(spread.scaled < kLossSpreadTolerancePoints1024);
}

TEST_CASE("print land share tables for erosion scaling", "[.erosion_scaling_table]")
{
    for (const int size : { 64, 128, 256, 1024 }) {
        std::printf("size %d (tectonic / after unscaled erosion / after scaled erosion, land %%)\n", size);
        for (std::uint64_t seed = 1; seed <= 8; ++seed) {
            const Shares s = measure(seed, size);
            std::printf("  seed %llu: %5.1f / %5.1f / %5.1f\n", static_cast<unsigned long long>(seed), s.tectonic, s.unscaled, s.scaled);
        }
    }
}
