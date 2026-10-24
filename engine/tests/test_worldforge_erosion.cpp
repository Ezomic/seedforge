#include <worldforge/erosion_map.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

TectonicMap makeBase(std::uint64_t seed, int w, int h, int plates)
{
    auto map = generateTectonics(seed, { w, h, plates });
    REQUIRE(map.has_value());
    return *map;
}

ErodedMap erode(const TectonicMap& base, const ErosionParams& params = {})
{
    auto out = erodeTectonics(base, params);
    REQUIRE(out.has_value());
    return *out;
}

double meanAbsDiff(const TectonicMap& base, const ErodedMap& eroded)
{
    std::int64_t sum = 0;
    for (std::size_t c = 0; c < base.heightM.size(); ++c) sum += std::abs(static_cast<int>(eroded.heightM[c]) - base.heightM[c]);
    return static_cast<double>(sum) / static_cast<double>(base.heightM.size());
}

template <typename Field>
double meanSlope(const Field& height, int w, int h)
{
    std::int64_t sum = 0;
    std::int64_t count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x + 1 < w; ++x) {
            sum += std::abs(static_cast<int>(height[static_cast<std::size_t>(y) * w + x + 1]) - height[static_cast<std::size_t>(y) * w + x]);
            ++count;
        }
    }
    return static_cast<double>(sum) / static_cast<double>(count);
}

std::int64_t totalHeight(const std::vector<std::int16_t>& v)
{
    std::int64_t s = 0;
    for (const auto x : v) s += x;
    return s;
}

}

TEST_CASE("erosion is deterministic across repeat runs", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 128, 96, 12);
    const auto a = erode(base);
    const auto b = erode(base);
    CHECK(a.heightM == b.heightM);
    CHECK(hashErodedMap(a) == hashErodedMap(b));
}

TEST_CASE("erosion golden hash is stable", "[worldforge][erosion]")
{
    const auto eroded = erode(makeBase(kSeed, 128, 96, 12));
    CHECK(hashErodedMap(eroded) == 0x6b99a236d445c886ull);
}

TEST_CASE("erosion does not mutate the input and keeps the grid", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 64, 48, 8);
    const auto copy = base;
    const auto eroded = erode(base);
    CHECK(base.heightM == copy.heightM);
    CHECK(eroded.width == 64);
    CHECK(eroded.height == 48);
    CHECK(eroded.seed == kSeed);
    CHECK(eroded.heightM.size() == 64u * 48u);
}

TEST_CASE("erosion actually changes the terrain and smooths it", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 128, 128, 12);
    const auto eroded = erode(base);
    CHECK(meanAbsDiff(base, eroded) > 1.0);
    CHECK(meanSlope(eroded.heightM, 128, 128) < meanSlope(base.heightM, 128, 128));
}

TEST_CASE("hydraulic erosion alone changes the terrain", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 128, 128, 12);
    ErosionParams p;
    p.thermalPasses = 0;
    CHECK(meanAbsDiff(base, erode(base, p)) > 0.5);
    p.strength = 0;
    CHECK(meanAbsDiff(base, erode(base, p)) < meanAbsDiff(base, erode(base, ErosionParams{ .thermalPasses = 0 })));
}

TEST_CASE("thermal erosion alone lowers the mean slope", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 128, 128, 12);
    ErosionParams p;
    p.strength = 0;
    p.thermalPasses = 4;
    p.talusM = 2;
    const auto eroded = erode(base, p);
    CHECK(meanAbsDiff(base, eroded) > 0.5);
    CHECK(meanSlope(eroded.heightM, 128, 128) < meanSlope(base.heightM, 128, 128));
}

TEST_CASE("zero iterations returns the input heights", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 64, 64, 8);
    ErosionParams p;
    p.iterations = 0;
    CHECK(erode(base, p).heightM == base.heightM);
}

TEST_CASE("different seeds erode differently", "[worldforge][erosion]")
{
    std::set<std::uint64_t> hashes;
    for (std::uint64_t s = 0; s < 8; ++s) hashes.insert(hashErodedMap(erode(makeBase(s, 64, 64, 8))));
    CHECK(hashes.size() == 8);
}

TEST_CASE("erosion keeps heights in range and roughly conserves mass", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 128, 128, 12);
    const auto eroded = erode(base);
    for (const auto v : eroded.heightM) {
        CHECK(v >= kTectonicsMinHeightM);
        CHECK(v <= kTectonicsMaxHeightM);
    }
    const double cells = static_cast<double>(base.heightM.size());
    const double drift = std::abs(static_cast<double>(totalHeight(eroded.heightM) - totalHeight(base.heightM))) / cells;
    CHECK(drift < 20.0);
}

TEST_CASE("erosion rejects malformed input and parameters", "[worldforge][erosion]")
{
    const auto base = makeBase(kSeed, 64, 64, 8);

    auto wrongLength = base;
    wrongLength.heightM.pop_back();
    CHECK_FALSE(erodeTectonics(wrongLength).has_value());

    auto empty = base;
    empty.heightM.clear();
    CHECK_FALSE(erodeTectonics(empty).has_value());

    auto hostile = base;
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::max();
    CHECK_FALSE(erodeTectonics(hostile).has_value());
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::min();
    CHECK_FALSE(erodeTectonics(hostile).has_value());

    auto tiny = base;
    tiny.width = 2;
    CHECK_FALSE(erodeTectonics(tiny).has_value());

    auto huge = base;
    huge.width = 1 << 30;
    huge.height = 1 << 30;
    CHECK_FALSE(erodeTectonics(huge).has_value());

    ErosionParams p;
    p.iterations = -1;
    CHECK_FALSE(erodeTectonics(base, p).has_value());
    p = {};
    p.iterations = kErosionMaxIterations + 1;
    CHECK_FALSE(erodeTectonics(base, p).has_value());
    p = {};
    p.iterations = kErosionMaxIterations;
    p.thermalPasses = kErosionMaxIterations;
    auto big = makeBase(kSeed, 1024, 1024, 8);
    CHECK_FALSE(erodeTectonics(big, p).has_value());
    p = {};
    p.strength = 257;
    CHECK_FALSE(erodeTectonics(base, p).has_value());
    p = {};
    p.talusM = -1;
    CHECK_FALSE(erodeTectonics(base, p).has_value());
    p = {};
    p.thermalRate = std::numeric_limits<int>::max();
    CHECK_FALSE(erodeTectonics(base, p).has_value());
}

TEST_CASE("erosion survives extreme parameters and extreme heights", "[worldforge][erosion]")
{
    auto base = makeBase(kSeed, 64, 64, 8);
    for (std::size_t c = 0; c < base.heightM.size(); ++c) base.heightM[c] = (c % 2 == 0) ? kTectonicsMaxHeightM : kTectonicsMinHeightM;

    ErosionParams p;
    p.iterations = 8;
    p.strength = kErosionMaxRate;
    p.erodeRate = kErosionMaxRate;
    p.depositRate = kErosionMaxRate;
    p.thermalRate = kErosionMaxRate;
    p.talusM = 0;
    const auto eroded = erode(base, p);
    for (const auto v : eroded.heightM) {
        CHECK(v >= kTectonicsMinHeightM);
        CHECK(v <= kTectonicsMaxHeightM);
    }

    p.talusM = kErosionMaxTalusM;
    CHECK(erodeTectonics(base, p).has_value());
}

TEST_CASE("erosion handles flat, minimum-size and non-square grids", "[worldforge][erosion]")
{
    auto flat = makeBase(kSeed, 16, 16, 2);
    std::fill(flat.heightM.begin(), flat.heightM.end(), std::int16_t{ 300 });
    CHECK(erode(flat).heightM == flat.heightM);

    const auto thin = makeBase(kSeed, 16, 200, 4);
    const auto eroded = erode(thin);
    CHECK(eroded.heightM.size() == 16u * 200u);
    CHECK(hashErodedMap(eroded) == hashErodedMap(erode(thin)));
}

TEST_CASE("thermal erosion at the default talus levels a spike into a slope", "[worldforge][erosion][talus]")
{
    auto base = makeBase(kSeed, 64, 64, 8);
    base.heightM.assign(base.heightM.size(), 100);
    const std::size_t spike = static_cast<std::size_t>(32) * 64 + 32;
    base.heightM[spike] = 400;

    const auto maxStep = [](const std::vector<std::int16_t>& v) {
        int worst = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x + 1 < 64; ++x) {
                const std::size_t c = static_cast<std::size_t>(y) * 64 + x;
                worst = std::max(worst, std::abs(v[c + 1] - v[c]));
                if (y + 1 < 64) worst = std::max(worst, std::abs(v[c + 64] - v[c]));
            }
        }
        return worst;
    };

    ErosionParams params;
    params.strength = 0;
    const auto eroded = erode(base, params);
    ErosionParams off = params;
    off.thermalPasses = 0;
    CHECK(maxStep(erode(base, off).heightM) == 300);
    CHECK(maxStep(eroded.heightM) <= params.talusM + 1);
    CHECK(eroded.heightM[spike] < 400);
}
