#include "ichor/traversal/vantage_reveal.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

float flat(std::int32_t, std::int32_t) { return 0.0f; }

bool has(const std::vector<VantageCell>& cells, std::int32_t x, std::int32_t y)
{
    return std::binary_search(cells.begin(), cells.end(), VantageCell{x, y}, vantageCellLess);
}

std::size_t circleCells(int r)
{
    std::size_t n = 0;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= r * r) {
                ++n;
            }
        }
    }
    return n;
}

}

TEST_CASE("flat terrain is revealed across the whole disc", "[vantage]")
{
    const VantageReveal r = computeVantageReveal({{10, -4}, 2.0f, 8.0f}, flat);
    CHECK(r.status == VantageStatus::Ok);
    CHECK(r.cells.size() == circleCells(8));
    CHECK(has(r.cells, 10, -4));
    CHECK(has(r.cells, 18, -4));
    CHECK_FALSE(has(r.cells, 19, -4));
    CHECK(std::is_sorted(r.cells.begin(), r.cells.end(), vantageCellLess));
}

TEST_CASE("a wall hides what lies behind it but not the wall", "[vantage]")
{
    auto wall = [](std::int32_t x, std::int32_t) { return x == 3 ? 10.0f : 0.0f; };
    const VantageReveal r = computeVantageReveal({{0, 0}, 2.0f, 8.0f}, wall);
    CHECK(has(r.cells, 2, 0));
    CHECK(has(r.cells, 3, 0));
    CHECK_FALSE(has(r.cells, 4, 0));
    CHECK_FALSE(has(r.cells, 7, 0));
    CHECK(has(r.cells, -7, 0));
    CHECK(r.cells.size() < circleCells(8));
}

TEST_CASE("a higher eye sees over the same wall", "[vantage]")
{
    auto wall = [](std::int32_t x, std::int32_t) { return x == 3 ? 4.0f : 0.0f; };
    CHECK_FALSE(has(computeVantageReveal({{0, 0}, 2.0f, 8.0f}, wall).cells, 5, 0));
    CHECK(has(computeVantageReveal({{0, 0}, 40.0f, 8.0f}, wall).cells, 5, 0));
}

TEST_CASE("a hill hides the valley behind it", "[vantage]")
{
    auto hill = [](std::int32_t x, std::int32_t y) { return (x - 4) * (x - 4) + y * y <= 4 ? 6.0f : 0.0f; };
    const VantageReveal r = computeVantageReveal({{0, 0}, 1.0f, 12.0f}, hill);
    CHECK(has(r.cells, 2, 0));
    CHECK_FALSE(has(r.cells, 4, 0));
    CHECK_FALSE(has(r.cells, 10, 0));
}

TEST_CASE("radius is capped and cost stays bounded", "[vantage]")
{
    const VantageReveal r = computeVantageReveal({{0, 0}, 2.0f, 1.0e9f}, flat);
    CHECK(r.status == VantageStatus::Ok);
    CHECK(r.cells.size() == circleCells(64));
    CHECK(r.samples <= kVantageDefaultSampleBudget);
}

TEST_CASE("a tiny sample budget truncates deterministically", "[vantage]")
{
    const VantageReveal a = computeVantageReveal({{0, 0}, 2.0f, 30.0f}, flat, 500);
    const VantageReveal b = computeVantageReveal({{0, 0}, 2.0f, 30.0f}, flat, 500);
    CHECK(a.status == VantageStatus::Truncated);
    CHECK(a.samples == 500);
    CHECK(a.cells == b.cells);
    CHECK(a.cells.size() < circleCells(30));
}

TEST_CASE("zero radius reveals only the vantage cell", "[vantage]")
{
    const VantageReveal r = computeVantageReveal({{5, 5}, 2.0f, 0.0f}, flat);
    REQUIRE(r.cells.size() == 1);
    CHECK(r.cells[0] == VantageCell{5, 5});
}

TEST_CASE("NaN, infinite and negative inputs are rejected", "[vantage]")
{
    const VantagePoint bad[] = {{{0, 0}, kNaN, 5.0f}, {{0, 0}, kInf, 5.0f}, {{0, 0}, 1.0f, kNaN},
        {{0, 0}, 1.0f, kInf}, {{0, 0}, 1.0f, -1.0f}};
    for (const VantagePoint& v : bad) {
        const VantageReveal r = computeVantageReveal(v, flat);
        CHECK(r.status == VantageStatus::InvalidInput);
        CHECK(r.cells.empty());
        CHECK(r.samples == 0);
    }
    CHECK(computeVantageReveal({{0, 0}, 1.0f, 5.0f}, VantageHeightFn{}).status == VantageStatus::InvalidInput);
}

TEST_CASE("non-finite and extreme samples are neither revealed nor crash", "[vantage]")
{
    auto odd = [](std::int32_t x, std::int32_t) { return x == 2 ? kNaN : (x == 3 ? 3.0e38f : 0.0f); };
    const VantageReveal r = computeVantageReveal({{0, 0}, 2.0f, 6.0f}, odd);
    CHECK(r.status == VantageStatus::Ok);
    CHECK_FALSE(has(r.cells, 2, 0));
    CHECK(has(r.cells, 3, 0));
    CHECK_FALSE(has(r.cells, 5, 0));
}

TEST_CASE("positions at the int32 edges do not overflow", "[vantage]")
{
    for (std::int32_t edge : {std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()}) {
        const VantageReveal r = computeVantageReveal({{edge, edge}, 2.0f, 5.0f}, flat);
        CHECK(r.status == VantageStatus::Ok);
        CHECK(has(r.cells, edge, edge));
        CHECK(r.cells.size() > 1);
    }
    CHECK(computeVantageReveal({{0, 0}, 3.0e38f, 5.0f}, flat).status == VantageStatus::Ok);
}

TEST_CASE("merging is idempotent and keeps the map sorted", "[vantage]")
{
    std::vector<VantageCell> map = {{9, 9}, {0, 0}};
    const VantageReveal r = computeVantageReveal({{0, 0}, 2.0f, 4.0f}, flat);
    CHECK(mergeRevealedCells(map, r.cells) == r.cells.size() - 1);
    CHECK(std::is_sorted(map.begin(), map.end(), vantageCellLess));
    CHECK(mergeRevealedCells(map, r.cells) == 0);
    CHECK(mergeRevealedCells(map, {}) == 0);
    const std::size_t size = map.size();
    CHECK(mergeRevealedCells(map, {{50, 50}, {50, 50}}) == 1);
    CHECK(map.size() == size + 1);
}

TEST_CASE("a second vantage adds only what the first missed", "[vantage]")
{
    auto wall = [](std::int32_t x, std::int32_t) { return x == 3 ? 10.0f : 0.0f; };
    std::vector<VantageCell> map;
    mergeRevealedCells(map, computeVantageReveal({{0, 0}, 2.0f, 8.0f}, wall).cells);
    CHECK_FALSE(has(map, 6, 0));
    CHECK(mergeRevealedCells(map, computeVantageReveal({{8, 0}, 2.0f, 8.0f}, wall).cells) > 0);
    CHECK(has(map, 6, 0));
}
