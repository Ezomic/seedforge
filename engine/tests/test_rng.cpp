#include "ichor/core/rng.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ichor;

namespace {

std::vector<std::uint32_t> draw(Rng& rng, int count)
{
    std::vector<std::uint32_t> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(rng.nextU32());
    }
    return out;
}

}

TEST_CASE("same seed gives the same sequence")
{
    Rng a(42);
    Rng b(42);
    REQUIRE(draw(a, 64) == draw(b, 64));
}

TEST_CASE("different seeds and streams differ")
{
    Rng a(42);
    Rng b(43);
    Rng c(42, 1);
    REQUIRE(draw(a, 16) != draw(b, 16));
    Rng a2(42);
    REQUIRE(draw(a2, 16) != draw(c, 16));
}

TEST_CASE("derived generators are independent of sibling order")
{
    Rng root(7);
    Rng combat = root.derive("combat");
    Rng weather = root.derive("weather");

    Rng root2(7);
    Rng weather2 = root2.derive("weather");
    Rng combat2 = root2.derive("combat");

    REQUIRE(draw(combat, 8) == draw(combat2, 8));
    REQUIRE(draw(weather, 8) == draw(weather2, 8));
}

TEST_CASE("range stays within bounds")
{
    Rng rng(3);
    for (int i = 0; i < 10000; ++i) {
        const auto v = rng.range(5, 9);
        REQUIRE(v >= 5);
        REQUIRE(v <= 9);
    }
    REQUIRE(rng.range(4, 4) == 4);
}

TEST_CASE("nextFloat is in [0, 1)")
{
    Rng rng(11);
    for (int i = 0; i < 10000; ++i) {
        const float f = rng.nextFloat();
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
    }
}
