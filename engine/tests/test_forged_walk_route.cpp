#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walk_route.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A square heightfield n samples to a side, one metre apart, heights in tenths of a metre.
Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

// A ridge across the world at x = 60..70, 2.5 m of rise a metre (far past the walk limit), with a gap
// where z lies in [gapLow, gapHigh]. An empty gap (low > high) makes it a wall.
Heightfield ridgeOf(std::uint32_t gapLow, std::uint32_t gapHigh)
{
    return gridOf(129, [gapLow, gapHigh](std::uint32_t x, std::uint32_t z) {
        const bool gap = z >= gapLow && z <= gapHigh;
        const int ridge = x < 60 || x > 70 || gap ? 0 : std::min(static_cast<int>(x) - 60, 70 - static_cast<int>(x)) * 25;
        return static_cast<std::uint16_t>(10000 + ridge);
    });
}

// Flat ground with a ring of ridge 12 to 20 m out from (40, 40) in either axis: the ground inside is a pocket.
Heightfield pocketOf()
{
    return gridOf(129, [](std::uint32_t x, std::uint32_t z) {
        const int d = std::max(std::abs(static_cast<int>(x) - 40), std::abs(static_cast<int>(z) - 40));
        const int ridge = d < 12 || d > 20 ? 0 : std::min(d - 12, 20 - d) * 25;
        return static_cast<std::uint16_t>(10000 + ridge);
    });
}

// Flat to x = 80, then a slope the walker cannot climb.
Heightfield cliffSideOf()
{
    return gridOf(129, [](std::uint32_t x, std::uint32_t) { return static_cast<std::uint16_t>(10000 + (x < 80 ? 0 : (x - 80) * 25)); });
}

double straightMetres(const glm::dvec3& a, const glm::dvec3& b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

}

TEST_CASE("over open ground the route is the straight line", "[walking][route]")
{
    const Heightfield field = ridgeOf(1, 0);
    // The ridge is there, so look at the flat half only.
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const glm::dvec3 from{5.0, 0.0, 20.0};
    const glm::dvec3 goal{50.0, 0.0, 90.0};
    const WalkRoute route = findWalkRoute(walking, from, goal);
    REQUIRE(route.found);
    REQUIRE(route.failure == WalkRouteFailure::None);
    REQUIRE(route.waypoints.size() >= 2);
    REQUIRE(route.waypoints.back().x == goal.x);
    REQUIRE(route.metres <= straightMetres(from, goal) * 1.05);
}

TEST_CASE("a route goes round a ridge by its gap, where the straight walk is stopped by it", "[walking][route]")
{
    const Heightfield field = ridgeOf(100, 110);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const glm::dvec3 from{20.0, 0.0, 64.0};
    const glm::dvec3 goal{110.0, 0.0, 64.0};

    const WalkResult straight = walkTo(walking, from, goal, {});
    REQUIRE(straight.outcome == WalkOutcome::Blocked);
    REQUIRE(straight.blocker == WalkBlock::TooSteep);

    WalkBlock block = WalkBlock::None;
    const double blockedAt = firstBlockOnLine(walking, from, goal, block);
    REQUIRE(block == WalkBlock::TooSteep);
    REQUIRE(blockedAt > 30.0);
    REQUIRE(blockedAt < 45.0);

    const WalkRoute route = findWalkRoute(walking, from, goal);
    REQUIRE(route.found);
    REQUIRE(route.metres > straightMetres(from, goal) * 1.3); // it really goes round: the gap is 40 m off the line
    const bool throughGap = std::any_of(route.waypoints.begin(), route.waypoints.end(),
        [](const glm::dvec3& p) { return p.x > 58.0 && p.x < 72.0 && p.z >= 99.0 && p.z <= 111.0; });
    REQUIRE(throughGap);

    WalkBotParams bot;
    bot.arriveMetres = 12.0;
    const WalkResult walked = walkTo(walking, from, goal, route.waypoints, bot);
    REQUIRE(walked.outcome == WalkOutcome::Arrived);
    REQUIRE(walked.metresLeft <= 12.0);
}

TEST_CASE("a wall with no gap has no route, and the report says what bounds the ground the start can reach", "[walking][route]")
{
    const Heightfield field = ridgeOf(1, 0);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const WalkRoute route = findWalkRoute(walking, {20.0, 0.0, 64.0}, {110.0, 0.0, 64.0});
    REQUIRE_FALSE(route.found);
    REQUIRE(route.failure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(route.waypoints.empty());
    const double half = 60.0 * 128.0;
    REQUIRE(route.reachableSquareMetres > 0.5 * half);
    REQUIRE(route.reachableSquareMetres < 1.1 * half);
    REQUIRE(route.boundarySteep > route.boundaryDeepWater);
    REQUIRE(route.boundaryMapEdge > 0);
    const std::string text = describeEnclosure(route);
    REQUIRE(text.find("enclosed") != std::string::npos);
    REQUIRE(text.find("cliffs") != std::string::npos);
}

TEST_CASE("a window that holds the whole pocket is not searched a second time over the world", "[walking][route]")
{
    const Heightfield field = pocketOf();
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    WalkRouteParams params;
    params.marginMetres = 30.0;
    const WalkRoute route = findWalkRoute(walking, {40.0, 0.0, 40.0}, {100.0, 0.0, 100.0}, params);
    REQUIRE_FALSE(route.found);
    REQUIRE(route.failure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(route.reachableSquareMetres > 0.0);
    REQUIRE(static_cast<double>(route.nodesSearched) * params.stepMetres * params.stepMetres <= 1.2 * route.reachableSquareMetres);
}

TEST_CASE("a window that cuts the ground off still gives way to the whole world", "[walking][route]")
{
    const Heightfield field = ridgeOf(1, 0);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    WalkRouteParams params;
    params.marginMetres = 10.0;
    const WalkRoute route = findWalkRoute(walking, {20.0, 0.0, 64.0}, {110.0, 0.0, 64.0}, params);
    REQUIRE_FALSE(route.found);
    REQUIRE(route.failure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(route.reachableSquareMetres > 0.5 * 60.0 * 128.0); // the window alone is 20 m across
}

TEST_CASE("a start shown to be shut in answers another goal at once, and still finds a goal inside the pocket", "[walking][route]")
{
    const Heightfield field = pocketOf();
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    WalkRouteParams params;
    params.marginMetres = 30.0;
    WalkRouteMemo memo;
    const glm::dvec3 from{40.0, 0.0, 40.0};
    const WalkRoute first = findWalkRoute(walking, from, {100.0, 0.0, 100.0}, params, &memo);
    REQUIRE(first.failure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(first.nodesSearched > 0);
    REQUIRE(memo.pockets.size() == 1);

    const WalkRoute again = findWalkRoute(walking, from, {110.0, 0.0, 20.0}, params, &memo);
    REQUIRE(again.failure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(again.nodesSearched == 0);
    REQUIRE(again.reachableSquareMetres == first.reachableSquareMetres);
    REQUIRE(again.boundarySteep == first.boundarySteep);
    REQUIRE(describeEnclosure(again) == describeEnclosure(first));

    const WalkRoute inside = findWalkRoute(walking, from, {46.0, 0.0, 46.0}, params, &memo);
    REQUIRE(inside.found);

    const WalkRoute elsewhere = findWalkRoute(walking, {10.0, 0.0, 10.0}, {110.0, 0.0, 20.0}, params, &memo);
    REQUIRE(elsewhere.found);
}

TEST_CASE("a start or goal on ground that cannot be walked is named, not searched blindly", "[walking][route]")
{
    const Heightfield side = cliffSideOf();
    const ForgedWalking walking(side, nullptr, CapabilitySet{});
    REQUIRE(findWalkRoute(walking, {120.0, 0.0, 64.0}, {20.0, 0.0, 64.0}).failure == WalkRouteFailure::StartBlocked);
    REQUIRE(findWalkRoute(walking, {20.0, 0.0, 64.0}, {120.0, 0.0, 64.0}).failure == WalkRouteFailure::GoalBlocked);
    REQUIRE(findWalkRoute(walking, {20.0, 0.0, 20.0}, {60.0, 0.0, 100.0}).found);
}

TEST_CASE("a route search survives hostile input and a tiny node budget", "[walking][route]")
{
    const Heightfield field = ridgeOf(100, 110);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const glm::dvec3 a{20.0, 0.0, 64.0};
    const glm::dvec3 b{110.0, 0.0, 64.0};
    for (const glm::dvec3& bad : {glm::dvec3{kNaN, 0.0, 5.0}, glm::dvec3{5.0, 0.0, kInf}, glm::dvec3{-10.0, 0.0, 5.0}, glm::dvec3{5.0, 0.0, 1.0e9}}) {
        REQUIRE(findWalkRoute(walking, bad, b).failure == WalkRouteFailure::BadInput);
        REQUIRE(findWalkRoute(walking, a, bad).failure == WalkRouteFailure::BadInput);
    }
    WalkRouteParams params;
    params.stepMetres = kNaN;
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.checkMetres = 0.0;
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.maxNodes = 0;
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.maxNodes = std::numeric_limits<std::size_t>::max();
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.goalRadiusMetres = 1.0e300;
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.stepMetres = 1.0e300;
    REQUIRE(findWalkRoute(walking, a, b, params).failure == WalkRouteFailure::BadInput);
    params = {};
    params.checkMetres = 0.05; // the probes are capped per move, so a fine spacing cannot run away
    REQUIRE(findWalkRoute(walking, a, b, params).found);

    params = {};
    params.maxNodes = 400; // the grid is widened to fit, the answer is still a route round the ridge
    const WalkRoute coarse = findWalkRoute(walking, a, b, params);
    REQUIRE(coarse.found);
    REQUIRE(coarse.nodesSearched <= 400);

    WalkBlock block = WalkBlock::None;
    REQUIRE(firstBlockOnLine(walking, {kNaN, 0.0, 0.0}, b, block) == 0.0);
    REQUIRE(block == WalkBlock::OffMap);
}

TEST_CASE("the same search gives the same route", "[walking][route]")
{
    const Heightfield field = ridgeOf(100, 110);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const WalkRoute one = findWalkRoute(walking, {20.0, 0.0, 64.0}, {110.0, 0.0, 64.0});
    const WalkRoute two = findWalkRoute(walking, {20.0, 0.0, 64.0}, {110.0, 0.0, 64.0});
    REQUIRE(one.found);
    REQUIRE(one.waypoints.size() == two.waypoints.size());
    for (std::size_t i = 0; i < one.waypoints.size(); ++i) {
        REQUIRE(one.waypoints[i].x == two.waypoints[i].x);
        REQUIRE(one.waypoints[i].z == two.waypoints[i].z);
    }
}
