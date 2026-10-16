#include <worldforge/region_tier_assign.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using namespace worldforge;

namespace {
constexpr std::uint64_t kWorld = 0x123456789abcdef0ull;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

RegionTierParams params(std::uint32_t tiers = 5, double width = 100.0, double jitter = 0.3, double dangerWeight = 0.5)
{
    return RegionTierParams{tiers, width, jitter, dangerWeight};
}

std::uint8_t one(std::uint64_t seed, const RegionTierParams& p, double distance, double danger = 0.0)
{
    const RegionTierInput in{distance, danger};
    const auto out = assignRegionTiers(seed, p, std::span<const RegionTierInput>(&in, 1));
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 1);
    return (*out)[0];
}
}

TEST_CASE("a region at the start is tier 0 and one far away is the top tier", "[worldforge][region_tier]")
{
    const auto p = params();
    CHECK(one(kWorld, p, 0.0) == 0);
    CHECK(one(kWorld, p, 1.0e9) == 4);
}

TEST_CASE("tier never decreases as distance grows, for any seed", "[worldforge][region_tier]")
{
    const auto p = params(8, 50.0, 0.9, 0.0);
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        std::uint8_t prev = 0;
        for (double d = 0.0; d < 2000.0; d += 0.5) {
            const auto t = one(seed, p, d);
            REQUIRE(t >= prev);
            prev = t;
        }
        CHECK(prev == 7);
    }
}

TEST_CASE("with danger fixed, tier is monotone in distance across a whole batch", "[worldforge][region_tier]")
{
    const auto p = params();
    std::vector<RegionTierInput> in;
    for (int i = 0; i < 500; ++i) in.push_back({i * 1.7, 0.4});
    const auto out = assignRegionTiers(kWorld, p, in);
    REQUIRE(out.has_value());
    for (std::size_t i = 1; i < out->size(); ++i) REQUIRE((*out)[i] >= (*out)[i - 1]);
}

TEST_CASE("danger raises the tier at the same distance", "[worldforge][region_tier]")
{
    const auto p = params(5, 100.0, 0.0, 1.0);
    std::uint8_t prev = 0;
    for (int step = 0; step <= 20; ++step) {
        const auto t = one(kWorld, p, 150.0, step / 20.0);
        REQUIRE(t >= prev);
        prev = t;
    }
    CHECK(one(kWorld, p, 150.0, 1.0) > one(kWorld, p, 150.0, 0.0));
}

TEST_CASE("danger is clamped to 0..1 and NaN danger counts as none", "[worldforge][region_tier]")
{
    const auto p = params(5, 100.0, 0.0, 1.0);
    CHECK(one(kWorld, p, 150.0, 7.0) == one(kWorld, p, 150.0, 1.0));
    CHECK(one(kWorld, p, 150.0, -3.0) == one(kWorld, p, 150.0, 0.0));
    CHECK(one(kWorld, p, 150.0, kNaN) == one(kWorld, p, 150.0, 0.0));
    CHECK(one(kWorld, p, 150.0, kInf) == one(kWorld, p, 150.0, 1.0));
}

TEST_CASE("the seed moves ring boundaries, so different worlds tier the same distance differently", "[worldforge][region_tier]")
{
    const auto p = params(6, 100.0, 0.9, 0.0);
    int differing = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
        for (double d = 0.0; d < 700.0; d += 10.0)
            if (one(seed, p, d) != one(0, p, d)) ++differing;
    CHECK(differing > 100);
}

TEST_CASE("zero jitter gives plain rings of equal width", "[worldforge][region_tier]")
{
    const auto p = params(4, 100.0, 0.0, 0.0);
    CHECK(one(kWorld, p, 99.9) == 0);
    CHECK(one(kWorld, p, 100.0) == 1);
    CHECK(one(kWorld, p, 199.9) == 1);
    CHECK(one(kWorld, p, 200.0) == 2);
    CHECK(one(kWorld, p, 300.0) == 3);
    CHECK(one(kWorld, p, 5000.0) == 3);
}

TEST_CASE("jitter keeps the first ring within its bound for every seed", "[worldforge][region_tier]")
{
    const auto p = params(3, 100.0, 0.5, 0.0);
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        CHECK(one(seed, p, 49.0) == 0);
        CHECK(one(seed, p, 151.0) >= 1);
    }
}

TEST_CASE("ties: equal inputs get equal tiers regardless of position or batch order", "[worldforge][region_tier]")
{
    const auto p = params();
    const std::vector<RegionTierInput> in = {{120.0, 0.2}, {120.0, 0.2}, {30.0, 0.0}, {120.0, 0.2}, {30.0, 0.0}};
    const auto a = assignRegionTiers(kWorld, p, in);
    REQUIRE(a.has_value());
    CHECK((*a)[0] == (*a)[1]);
    CHECK((*a)[1] == (*a)[3]);
    CHECK((*a)[2] == (*a)[4]);

    const std::vector<RegionTierInput> rev(in.rbegin(), in.rend());
    const auto b = assignRegionTiers(kWorld, p, rev);
    REQUIRE(b.has_value());
    for (std::size_t i = 0; i < in.size(); ++i) CHECK((*a)[i] == (*b)[in.size() - 1 - i]);
}

TEST_CASE("a boundary distance lands in the outer tier", "[worldforge][region_tier]")
{
    const auto p = params(3, 100.0, 0.0, 0.0);
    CHECK(one(kWorld, p, 100.0) == 1);
    CHECK(one(kWorld, p, std::nextafter(100.0, 0.0)) == 0);
}

TEST_CASE("NaN, negative and infinite distances are handled without crashing", "[worldforge][region_tier]")
{
    const auto p = params();
    CHECK(one(kWorld, p, kNaN) == 4);
    CHECK(one(kWorld, p, kInf) == 4);
    CHECK(one(kWorld, p, -1.0) == 0);
    CHECK(one(kWorld, p, -kInf) == 0);
    CHECK(one(kWorld, p, -0.0) == 0);
    CHECK(one(kWorld, p, std::numeric_limits<double>::max(), 1.0) == 4);
    CHECK(one(kWorld, p, std::numeric_limits<double>::denorm_min()) == 0);
}

TEST_CASE("hostile parameters are rejected rather than producing a tiering", "[worldforge][region_tier]")
{
    const RegionTierInput in{10.0, 0.0};
    const std::span<const RegionTierInput> span(&in, 1);
    CHECK_FALSE(assignRegionTiers(kWorld, params(0), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(kMaxRegionTiers + 1), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 0.0), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, -5.0), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, kNaN), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, kInf), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, -0.1), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, 1.0), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, kNaN), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, 0.3, -1.0), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, 0.3, kNaN), span).has_value());
    CHECK_FALSE(assignRegionTiers(kWorld, params(5, 100.0, 0.3, kInf), span).has_value());
}

TEST_CASE("a single tier puts every region in tier 0, and the maximum tier count works", "[worldforge][region_tier]")
{
    CHECK(one(kWorld, params(1), 1.0e12, 1.0) == 0);
    const auto p = params(kMaxRegionTiers, 1.0, 0.9, 0.0);
    CHECK(one(kWorld, p, 1.0e9) == kMaxRegionTiers - 1);
    CHECK(one(kWorld, p, 0.0) == 0);
}

TEST_CASE("an empty batch yields an empty result", "[worldforge][region_tier]")
{
    const auto out = assignRegionTiers(kWorld, params(), std::span<const RegionTierInput>());
    REQUIRE(out.has_value());
    CHECK(out->empty());
}

TEST_CASE("a million regions are tiered, monotone, and identical on repeat", "[worldforge][region_tier]")
{
    const auto p = params(7, 25000.0, 0.4, 0.0);
    std::vector<RegionTierInput> in(1000000);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = {static_cast<double>(i), 0.0};
    const auto a = assignRegionTiers(kWorld, p, in);
    const auto b = assignRegionTiers(kWorld, p, in);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    REQUIRE(a->size() == in.size());
    for (std::size_t i = 1; i < a->size(); ++i) REQUIRE((*a)[i] >= (*a)[i - 1]);
    CHECK(a->front() == 0);
    CHECK(a->back() == 6);
}

TEST_CASE("every tier is populated across a realistic world", "[worldforge][region_tier]")
{
    const auto p = params(5, 100.0, 0.3, 0.0);
    std::vector<int> counts(5, 0);
    for (int i = 0; i < 1000; ++i) ++counts[one(kWorld, p, i * 0.6)];
    for (const int c : counts) CHECK(c > 50);
}

TEST_CASE("repeat calls return identical results and boundaries strictly increase", "[worldforge][region_tier]")
{
    const auto p = params();
    std::vector<RegionTierInput> in;
    for (int i = 0; i < 300; ++i) in.push_back({i * 2.3, (i % 10) / 10.0});
    const auto a = assignRegionTiers(kWorld, p, in);
    const auto b = assignRegionTiers(kWorld, p, in);
    REQUIRE(a.has_value());
    CHECK(*a == *b);

    const auto edges = regionTierBoundaries(kWorld, p);
    REQUIRE(edges.has_value());
    CHECK(edges->size() == 4);
    CHECK(*edges == *regionTierBoundaries(kWorld, p));
    CHECK((*edges)[0] > 0.0);
    for (std::size_t i = 1; i < edges->size(); ++i) CHECK((*edges)[i] > (*edges)[i - 1]);
}
