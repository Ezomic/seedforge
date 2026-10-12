#include "ichor/core/fixed_step.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ichor;

TEST_CASE("fixed step runs whole steps and keeps the remainder")
{
    // Powers of two, so the accumulator arithmetic is exact.
    FixedStep step(0.25);
    REQUIRE(step.advance(0.625) == 2);
    REQUIRE(step.alpha() == Catch::Approx(0.5));
    REQUIRE(step.advance(0.125) == 1);
    REQUIRE(step.alpha() == Catch::Approx(0.0));
}

TEST_CASE("a stall is capped and the backlog dropped")
{
    FixedStep step(0.25, 4);
    REQUIRE(step.advance(10.0) == 4);
    REQUIRE(step.advance(0.0) == 0);
    REQUIRE(step.alpha() == Catch::Approx(0.0));
}

TEST_CASE("negative frame time is ignored")
{
    FixedStep step(0.25);
    REQUIRE(step.advance(-5.0) == 0);
    REQUIRE(step.advance(0.25) == 1);
}
