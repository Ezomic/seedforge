#include <worldforge/erosion_map.h>
#include <worldforge/landmark_coverage.h>
#include <worldforge/tectonics_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;
constexpr int kSize = 64;

std::vector<std::int16_t> flatWorld(int w, int h, std::int16_t z)
{
    return std::vector<std::int16_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), z);
}

void addRidge(std::vector<std::int16_t>& z, int w, int h, int x0, int x1, std::int16_t top)
{
    for (int y = 0; y < h; ++y) {
        for (int x = x0; x <= x1; ++x) z[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = top;
    }
}

LandmarkCoverageReport run(const std::vector<std::int16_t>& z, int w, int h, const std::vector<Landmark>& marks, const LandmarkCoverageParams& p = {})
{
    auto r = validateLandmarkCoverage({ w, h, z, marks }, p);
    REQUIRE(r.has_value());
    return *r;
}

LandmarkCoverageParams farOnly()
{
    LandmarkCoverageParams p;
    p.nearRangeCells = 0;
    p.viewRangeCells = 200;
    return p;
}

}

TEST_CASE("landmark coverage: a landmark on open ground covers the whole world", "[worldforge][landmark]")
{
    const auto z = flatWorld(kSize, kSize, 100);
    const auto r = run(z, kSize, kSize, { { 32, 32, 50 } }, farOnly());
    CHECK(r.stride == 1);
    CHECK(r.sampleCount == kSize * kSize);
    CHECK(r.coveragePermille == 1000);
    CHECK(r.passed);
    CHECK(r.gaps.empty());
}

TEST_CASE("landmark coverage: removing the landmarks drops coverage to zero", "[worldforge][landmark]")
{
    const auto z = flatWorld(kSize, kSize, 100);
    const auto with = run(z, kSize, kSize, { { 32, 32, 50 } }, farOnly());
    const auto without = run(z, kSize, kSize, {}, farOnly());
    CHECK(with.coveragePermille == 1000);
    CHECK(without.coveragePermille == 0);
    CHECK_FALSE(without.passed);
    CHECK(without.gaps.size() == kLandmarkMaxGaps);
}

TEST_CASE("landmark coverage: a landmark behind a ridge is not counted", "[worldforge][landmark]")
{
    auto open = flatWorld(kSize, kSize, 100);
    auto ridged = open;
    addRidge(ridged, kSize, kSize, 40, 42, 600);
    const std::vector<Landmark> marks = { { 10, 32, 50 } };

    const auto openR = run(open, kSize, kSize, marks, farOnly());
    const auto ridgedR = run(ridged, kSize, kSize, marks, farOnly());
    CHECK(openR.coveragePermille == 1000);
    CHECK(ridgedR.coveragePermille < 700);
    CHECK(ridgedR.coveragePermille > 500);
    CHECK_FALSE(ridgedR.passed);
    for (const CoverageSample& g : ridgedR.gaps) CHECK(g.x > 40);
}

TEST_CASE("landmark coverage: a tall enough landmark rises over the ridge", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, 100);
    addRidge(z, kSize, kSize, 40, 42, 600);
    const auto low = run(z, kSize, kSize, { { 10, 32, 50 } }, farOnly());
    const auto tall = run(z, kSize, kSize, { { 10, 32, 1500 } }, farOnly());
    CHECK(tall.coveragePermille >= 700);
    CHECK(tall.coveragePermille > low.coveragePermille);
}

TEST_CASE("landmark coverage: the near rule covers a walk without line of sight", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, 100);
    addRidge(z, kSize, kSize, 40, 42, 600);
    LandmarkCoverageParams p = farOnly();
    const auto seenR = run(z, kSize, kSize, { { 10, 32, 50 } }, p);
    p.nearRangeCells = 255;
    const auto nearR = run(z, kSize, kSize, { { 10, 32, 50 } }, p);
    CHECK(nearR.coveragePermille == 1000);
    CHECK(nearR.nearCount > 0);
    CHECK(nearR.coveragePermille > seenR.coveragePermille);
}

TEST_CASE("landmark coverage: view range limits what counts as visible", "[worldforge][landmark]")
{
    const auto z = flatWorld(kSize, kSize, 100);
    LandmarkCoverageParams p = farOnly();
    p.viewRangeCells = 10;
    const auto r = run(z, kSize, kSize, { { 32, 32, 50 } }, p);
    CHECK(r.coveragePermille > 0);
    CHECK(r.coveragePermille < 200);
}

TEST_CASE("landmark coverage: adding landmarks only raises coverage", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, 100);
    addRidge(z, kSize, kSize, 40, 42, 600);
    const auto one = run(z, kSize, kSize, { { 10, 32, 50 } }, farOnly());
    const auto two = run(z, kSize, kSize, { { 10, 32, 50 }, { 55, 32, 50 } }, farOnly());
    CHECK(two.coveragePermille > one.coveragePermille);
    CHECK(two.coveragePermille >= 950);
}

TEST_CASE("landmark coverage: ocean is not sampled and an all-ocean world fails", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, 100);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize / 2; ++x) z[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)] = -50;
    }
    const auto half = run(z, kSize, kSize, { { 48, 32, 50 } }, farOnly());
    CHECK(half.sampleCount == kSize * kSize / 2);

    const auto sea = run(flatWorld(kSize, kSize, -10), kSize, kSize, { { 5, 5, 50 } });
    CHECK(sea.sampleCount == 0);
    CHECK(sea.coveragePermille == 0);
    CHECK_FALSE(sea.passed);
}

TEST_CASE("landmark coverage: the sample count is bounded by maxSamples", "[worldforge][landmark]")
{
    const auto z = flatWorld(1024, 1024, 100);
    LandmarkCoverageParams p;
    p.maxSamples = 1000;
    const auto r = run(z, 1024, 1024, { { 512, 512, 80 } }, p);
    CHECK(r.sampleCount <= 1000);
    CHECK(r.sampleCount > 0);
    CHECK(r.stride > 1);
}

TEST_CASE("landmark coverage: results are deterministic on a generated world", "[worldforge][landmark]")
{
    auto t = generateTectonics(kSeed, { 128, 128, 6 });
    REQUIRE(t.has_value());
    auto e = erodeTectonics(*t);
    REQUIRE(e.has_value());

    std::vector<Landmark> marks;
    for (int y = 16; y < 128; y += 32) {
        for (int x = 16; x < 128; x += 32) {
            if (e->heightM[e->index(x, y)] > 0) marks.push_back({ x, y, 120 });
        }
    }
    REQUIRE_FALSE(marks.empty());

    LandmarkCoverageParams p;
    const auto a = validateLandmarkCoverage({ 128, 128, e->heightM, marks }, p);
    const auto b = validateLandmarkCoverage({ 128, 128, e->heightM, marks }, p);
    const auto none = validateLandmarkCoverage({ 128, 128, e->heightM, {} }, p);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(none.has_value());
    CHECK(hashLandmarkCoverage(*a) == hashLandmarkCoverage(*b));
    CHECK(a->coveredCount == a->nearCount + a->visibleCount);
    CHECK(a->coveragePermille > none->coveragePermille);
    CHECK(none->coveragePermille == 0);
}

TEST_CASE("landmark coverage: hostile inputs are rejected", "[worldforge][landmark]")
{
    const auto z = flatWorld(kSize, kSize, 100);
    const std::vector<Landmark> ok = { { 5, 5, 50 } };
    const LandmarkCoverageParams p;

    CHECK_FALSE(validateLandmarkCoverage({ 8, 8, flatWorld(8, 8, 100), ok }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ 2048, 2048, z, ok }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, std::span<const std::int16_t>(z).first(10), ok }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ 0, 0, {}, {} }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>{ { -1, 5, 50 } } }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>{ { 5, kSize, 50 } } }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>{ { 5, 5, 0 } } }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>{ { 5, 5, 100000 } } }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>{ { 5, 5, std::numeric_limits<int>::max() } } }, p).has_value());
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, z, std::vector<Landmark>(kLandmarkMaxLandmarks + 1, Landmark{ 5, 5, 50 }) }, p).has_value());

    auto bad = z;
    bad[3] = 30000;
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, bad, ok }, p).has_value());
    bad[3] = std::numeric_limits<std::int16_t>::min();
    CHECK_FALSE(validateLandmarkCoverage({ kSize, kSize, bad, ok }, p).has_value());
}

TEST_CASE("landmark coverage: out-of-range parameters are rejected", "[worldforge][landmark]")
{
    const auto z = flatWorld(kSize, kSize, 100);
    const std::vector<Landmark> ok = { { 5, 5, 50 } };
    const auto bad = [&](auto mutate) {
        LandmarkCoverageParams p;
        mutate(p);
        return validateLandmarkCoverage({ kSize, kSize, z, ok }, p).has_value();
    };
    CHECK_FALSE(bad([](auto& p) { p.maxSamples = 0; }));
    CHECK_FALSE(bad([](auto& p) { p.maxSamples = kLandmarkMaxSamples + 1; }));
    CHECK_FALSE(bad([](auto& p) { p.maxSamples = -1; }));
    CHECK_FALSE(bad([](auto& p) { p.viewRangeCells = 0; }));
    CHECK_FALSE(bad([](auto& p) { p.viewRangeCells = 256; }));
    CHECK_FALSE(bad([](auto& p) { p.nearRangeCells = -1; }));
    CHECK_FALSE(bad([](auto& p) { p.nearRangeCells = std::numeric_limits<int>::max(); }));
    CHECK_FALSE(bad([](auto& p) { p.eyeHeightM = -1; }));
    CHECK_FALSE(bad([](auto& p) { p.eyeHeightM = kLandmarkMaxEyeM + 1; }));
    CHECK_FALSE(bad([](auto& p) { p.minCoveragePermille = 1001; }));
    CHECK_FALSE(bad([](auto& p) { p.minCoveragePermille = -1; }));
    CHECK_FALSE(bad([](auto& p) { p.seaLevelM = 20000; }));
}

TEST_CASE("landmark coverage: extreme terrain and duplicate landmarks stay well defined", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, static_cast<std::int16_t>(kTectonicsMaxHeightM));
    z[0] = static_cast<std::int16_t>(kTectonicsMinHeightM);
    const std::vector<Landmark> dup(8, Landmark{ 20, 20, kLandmarkMaxHeightM });
    LandmarkCoverageParams p;
    p.viewRangeCells = kLandmarkMaxRangeCells;
    p.nearRangeCells = kLandmarkMaxRangeCells;
    const auto r = run(z, kSize, kSize, dup, p);
    CHECK(r.coveragePermille == 1000);

    p.nearRangeCells = 0;
    const auto seen = run(z, kSize, kSize, dup, p);
    CHECK(seen.coveragePermille == 1000);
}

TEST_CASE("landmark coverage: eye height clears a low ridge", "[worldforge][landmark]")
{
    auto z = flatWorld(kSize, kSize, 100);
    addRidge(z, kSize, kSize, 30, 30, 104);
    LandmarkCoverageParams p = farOnly();
    p.eyeHeightM = 0;
    const auto low = run(z, kSize, kSize, { { 10, 32, 20 } }, p);
    p.eyeHeightM = kLandmarkMaxEyeM;
    const auto high = run(z, kSize, kSize, { { 10, 32, 20 } }, p);
    CHECK(high.coveragePermille > low.coveragePermille);
}

TEST_CASE("landmark coverage: non-square maps and strided samples index correctly", "[worldforge][landmark]")
{
    const int w = 200;
    const int h = 40;
    auto z = flatWorld(w, h, 100);
    LandmarkCoverageParams p = farOnly();
    p.maxSamples = 500;
    p.viewRangeCells = 60;
    const auto r = run(z, w, h, { { 190, 35, 30 } }, p);
    CHECK(r.stride > 1);
    CHECK(r.sampleCount <= 500);
    CHECK(r.coveragePermille > 0);
    CHECK(r.coveragePermille < 1000);
    REQUIRE_FALSE(r.gaps.empty());
    CHECK(r.gaps.size() == kLandmarkMaxGaps);
    CHECK(r.gaps.front().x == r.stride / 2);
    CHECK(r.gaps.front().y == r.stride / 2);
    for (std::size_t i = 1; i < r.gaps.size(); ++i) CHECK((r.gaps[i].y > r.gaps[i - 1].y || (r.gaps[i].y == r.gaps[i - 1].y && r.gaps[i].x > r.gaps[i - 1].x)));
    for (const CoverageSample& g : r.gaps) CHECK(((g.x - r.stride / 2) % r.stride == 0 && g.x < w && g.y < h));
}
