#include "ichor/world/placement_clear.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

ClearRadiusRules rules(double clear = 5.0, double extent = 100.0)
{
    return {clear, {extent}};
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}

TEST_CASE("an empty field leaves a candidate clear", "[placement]")
{
    const auto check = checkPlacement({50, 50}, rules(), {});
    CHECK(check.verdict == PlacementVerdict::Clear);
    CHECK(check.blockerCount == 0);
}

TEST_CASE("an obstacle inside the clear disc blocks, outside does not", "[placement]")
{
    const std::vector<PlacementObstacle> near{{53, 50, 1.0, ObstacleKind::Rock}};
    const auto blocked = checkPlacement({50, 50}, rules(), near);
    CHECK(blocked.verdict == PlacementVerdict::Blocked);
    CHECK(blocked.firstBlocker == 0);
    CHECK(blocked.blockerCount == 1);

    const std::vector<PlacementObstacle> far{{60, 50, 1.0, ObstacleKind::Rock}};
    CHECK(checkPlacement({50, 50}, rules(), far).verdict == PlacementVerdict::Clear);
}

TEST_CASE("the obstacle radius widens its reach", "[placement]")
{
    const std::vector<PlacementObstacle> point{{58, 50, 0.0, ObstacleKind::Foliage}};
    const std::vector<PlacementObstacle> wide{{58, 50, 4.0, ObstacleKind::Foliage}};
    CHECK(checkPlacement({50, 50}, rules(), point).verdict == PlacementVerdict::Clear);
    CHECK(checkPlacement({50, 50}, rules(), wide).verdict == PlacementVerdict::Blocked);
}

TEST_CASE("touching discs are clear, a hair of overlap is blocked", "[placement]")
{
    const std::vector<PlacementObstacle> touching{{58, 50, 3.0, ObstacleKind::Water}};
    CHECK(checkPlacement({50, 50}, rules(), touching).verdict == PlacementVerdict::Clear);
    const std::vector<PlacementObstacle> over{{58, 50, 3.0000001, ObstacleKind::Water}};
    CHECK(checkPlacement({50, 50}, rules(), over).verdict == PlacementVerdict::Blocked);
}

TEST_CASE("every kind blocks and the first blocker is the lowest index", "[placement]")
{
    const std::vector<PlacementObstacle> field{
        {90, 90, 1, ObstacleKind::Foliage},
        {52, 50, 1, ObstacleKind::Cliff},
        {50, 52, 1, ObstacleKind::Water},
        {48, 50, 1, ObstacleKind::Rock},
    };
    const auto check = checkPlacement({50, 50}, rules(), field);
    CHECK(check.verdict == PlacementVerdict::Blocked);
    CHECK(check.firstBlocker == 1);
    CHECK(check.blockerCount == 3);
}

TEST_CASE("a clear disc leaving the world is out of bounds", "[placement]")
{
    CHECK(checkPlacement({3, 50}, rules(), {}).verdict == PlacementVerdict::OutOfBounds);
    CHECK(checkPlacement({50, 97}, rules(), {}).verdict == PlacementVerdict::OutOfBounds);
    CHECK(checkPlacement({5, 95}, rules(), {}).verdict == PlacementVerdict::Clear);
}

TEST_CASE("non-finite or absurd candidates are invalid", "[placement]")
{
    CHECK(checkPlacement({kNaN, 50}, rules(), {}).verdict == PlacementVerdict::Invalid);
    CHECK(checkPlacement({50, kInf}, rules(), {}).verdict == PlacementVerdict::Invalid);
    CHECK(checkPlacement({-kInf, 50}, rules(), {}).verdict == PlacementVerdict::Invalid);
    CHECK(checkPlacement({1e300, 50}, rules(), {}).verdict == PlacementVerdict::Invalid);
}

TEST_CASE("hostile rules are reported and never pass a candidate", "[placement]")
{
    CHECK(validateClearRadiusRules(rules()).empty());
    for (const auto& bad : {rules(0.0), rules(-1.0), rules(kNaN), rules(kInf), rules(5.0, 0.0), rules(5.0, kNaN), rules(5.0, 1e300), rules(60.0, 100.0)}) {
        CHECK_FALSE(validateClearRadiusRules(bad).empty());
        CHECK(checkPlacement({50, 50}, bad, {}).verdict == PlacementVerdict::Invalid);
    }
}

TEST_CASE("hostile obstacles are reported and make the check invalid", "[placement]")
{
    CHECK(validateObstacles({}).empty());
    const std::vector<std::vector<PlacementObstacle>> bad{
        {{kNaN, 0, 1, ObstacleKind::Rock}},
        {{0, kInf, 1, ObstacleKind::Rock}},
        {{0, 0, -1, ObstacleKind::Rock}},
        {{0, 0, kNaN, ObstacleKind::Rock}},
        {{0, 0, 1e300, ObstacleKind::Rock}},
        {{1e300, 0, 1, ObstacleKind::Rock}},
    };
    for (const auto& field : bad) {
        CHECK_FALSE(validateObstacles(field).empty());
        CHECK(checkPlacement({50, 50}, rules(), field).verdict == PlacementVerdict::Invalid);
    }
}

TEST_CASE("the obstacle list is capped", "[placement]")
{
    const std::vector<PlacementObstacle> tooMany(kMaxPlacementObstacles + 1, PlacementObstacle{1, 1, 1, ObstacleKind::Rock});
    CHECK_FALSE(validateObstacles(tooMany).empty());
}

TEST_CASE("filtering keeps the clear candidates in input order", "[placement]")
{
    const std::vector<PlacementObstacle> field{{20, 20, 2, ObstacleKind::Foliage}};
    const std::vector<PlacementCandidate> candidates{{21, 21}, {50, 50}, {2, 50}, {20, 40}, {kNaN, 1}, {80, 80}};
    std::vector<std::string> problems;
    const auto kept = filterClearPlacements(candidates, rules(), field, problems);
    CHECK(kept == std::vector<std::size_t>{1, 3, 5});
    CHECK(problems.size() == 1);
}

TEST_CASE("filtering with bad rules or obstacles yields nothing and says why", "[placement]")
{
    std::vector<std::string> problems;
    CHECK(filterClearPlacements({{50, 50}}, rules(-1.0), {}, problems).empty());
    CHECK_FALSE(problems.empty());

    problems.clear();
    CHECK(filterClearPlacements({{50, 50}}, rules(), {{kNaN, 0, 1, ObstacleKind::Rock}}, problems).empty());
    CHECK_FALSE(problems.empty());

    problems.clear();
    CHECK(filterClearPlacements({}, rules(), {}, problems).empty());
    CHECK(problems.empty());
}

TEST_CASE("removing the obstacle changes the outcome", "[placement]")
{
    const std::vector<PlacementCandidate> candidates{{50, 50}};
    std::vector<std::string> problems;
    CHECK(filterClearPlacements(candidates, rules(), {{51, 50, 1, ObstacleKind::Rock}}, problems).empty());
    CHECK(filterClearPlacements(candidates, rules(), {}, problems).size() == 1);
}

TEST_CASE("kind names are distinct", "[placement]")
{
    CHECK(obstacleKindName(ObstacleKind::Foliage) != obstacleKindName(ObstacleKind::Rock));
    CHECK(obstacleKindName(ObstacleKind::Water) != obstacleKindName(ObstacleKind::Cliff));
}
