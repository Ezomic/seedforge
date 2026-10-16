#include "ichor/traversal/travel_cost.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace ichor;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

TravelPlan plan(std::uint64_t length, std::size_t settlements)
{
    TravelPlan p;
    p.allowed = true;
    p.totalLength = length;
    for (std::size_t i = 0; i < settlements; ++i)
        p.path.push_back("s" + std::to_string(i));
    return p;
}

const TravelCostRates kRates{2, 5, 3, 7};

}

TEST_CASE("cost is per length plus per hop", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(20, 3), kRates, 1000);
    REQUIRE(c.affordable);
    CHECK(c.coin == 20 * 2 + 2 * 5);
    CHECK(c.ticks == 20 * 3 + 2 * 7);
    CHECK_FALSE(c.saturated);
}

TEST_CASE("a zero length path still pays the per hop fare", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(0, 2), kRates, 5);
    REQUIRE(c.affordable);
    CHECK(c.coin == 5);
    CHECK(c.ticks == 7);
}

TEST_CASE("zero rates make travel free", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(1000, 4), TravelCostRates{}, 0);
    REQUIRE(c.affordable);
    CHECK(c.coin == 0);
    CHECK(c.ticks == 0);
}

TEST_CASE("a refused plan is not priced", "[travel_cost]")
{
    const TravelCost c = travelCost(TravelPlan{}, kRates, 1000);
    CHECK_FALSE(c.affordable);
    CHECK(c.refusal == TravelCostRefusal::NotAllowed);
    CHECK(c.coin == 0);
}

TEST_CASE("an allowed plan with no road is malformed", "[travel_cost]")
{
    CHECK(travelCost(plan(10, 0), kRates, 1000).refusal == TravelCostRefusal::MalformedPlan);
    CHECK(travelCost(plan(10, 1), kRates, 1000).refusal == TravelCostRefusal::MalformedPlan);
}

TEST_CASE("insufficient coin refuses but still reports the cost", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(20, 3), kRates, 49);
    CHECK_FALSE(c.affordable);
    CHECK(c.refusal == TravelCostRefusal::InsufficientCoin);
    CHECK(c.coin == 50);
    CHECK(c.ticks == 74);
    CHECK(travelCost(plan(20, 3), kRates, 50).affordable);
}

TEST_CASE("extreme lengths and rates saturate instead of wrapping", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(kMax, 3), TravelCostRates{kMax, kMax, kMax, kMax}, kMax);
    CHECK(c.coin == kMax);
    CHECK(c.ticks == kMax);
    CHECK(c.saturated);
}

TEST_CASE("a saturated cost is not affordable below the ceiling", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(kMax, 2), TravelCostRates{2, 0, 0, 0}, kMax - 1);
    CHECK(c.saturated);
    CHECK_FALSE(c.affordable);
}

TEST_CASE("a long path of many hops is priced linearly", "[travel_cost]")
{
    const TravelCost c = travelCost(plan(1'000'000, 1001), kRates, kMax);
    CHECK(c.coin == 2'000'000 + 1000 * 5);
    CHECK(c.ticks == 3'000'000 + 1000 * 7);
}

TEST_CASE("the cost grows with distance", "[travel_cost]")
{
    const TravelCost near = travelCost(plan(10, 2), kRates, kMax);
    const TravelCost far = travelCost(plan(500, 2), kRates, kMax);
    CHECK(far.coin > near.coin);
    CHECK(far.ticks > near.ticks);
}

TEST_CASE("clampedTicks rejects bad doubles and clamps", "[travel_cost]")
{
    CHECK(clampedTicks(std::nan("")) == 0);
    CHECK(clampedTicks(-1.0) == 0);
    CHECK(clampedTicks(-std::numeric_limits<double>::infinity()) == 0);
    CHECK(clampedTicks(std::numeric_limits<double>::infinity()) == kMax);
    CHECK(clampedTicks(1e300) == kMax);
    CHECK(clampedTicks(0.0) == 0);
    CHECK(clampedTicks(12.9) == 12);
    CHECK(clampedTicks(18446744073709551616.0) == kMax);
}

TEST_CASE("advanceClock saturates", "[travel_cost]")
{
    CHECK(advanceClock(100, 50) == 150);
    CHECK(advanceClock(kMax, 1) == kMax);
    CHECK(advanceClock(kMax - 1, kMax) == kMax);
    CHECK(advanceClock(0, 0) == 0);
}

TEST_CASE("linkLengthFromDistance rejects bad doubles and clamps", "[travel_cost]")
{
    CHECK(linkLengthFromDistance(std::nan("")) == 0);
    CHECK(linkLengthFromDistance(-5.0) == 0);
    CHECK(linkLengthFromDistance(std::numeric_limits<double>::infinity()) == std::numeric_limits<std::uint32_t>::max());
    CHECK(linkLengthFromDistance(1e20) == std::numeric_limits<std::uint32_t>::max());
    CHECK(linkLengthFromDistance(41.7) == 41);
}
