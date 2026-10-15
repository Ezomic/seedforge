#include "ichor/traversal/fast_travel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

using Path = std::vector<std::string>;

// a - b - c in a line, d off to the side of b, e alone.
TravelNetwork town()
{
    TravelNetwork n;
    n.settlements = {{"a", true}, {"b", true}, {"c", true}, {"d", true}, {"e", true}};
    n.links = {{"a", "b", 10, true}, {"b", "c", 10, true}, {"b", "d", 5, true}};
    return n;
}

}

TEST_CASE("fast travel follows secured roads between discovered settlements", "[fast_travel]")
{
    const TravelPlan plan = planFastTravel(town(), "a", "c");
    REQUIRE(plan.allowed);
    CHECK(plan.refusal == TravelRefusal::None);
    CHECK(plan.path == Path{"a", "b", "c"});
    CHECK(plan.totalLength == 20);
    CHECK(planFastTravel(town(), "c", "a").path == Path{"c", "b", "a"});
}

TEST_CASE("an undiscovered destination or start is refused", "[fast_travel]")
{
    TravelNetwork n = town();
    n.settlements[2].discovered = false;
    CHECK(planFastTravel(n, "a", "c").refusal == TravelRefusal::NotDiscovered);
    CHECK(planFastTravel(n, "c", "a").refusal == TravelRefusal::NotDiscovered);
    CHECK_FALSE(planFastTravel(n, "a", "c").allowed);
    CHECK(planFastTravel(n, "a", "c").path.empty());
}

TEST_CASE("a route may not pass through an undiscovered settlement", "[fast_travel]")
{
    TravelNetwork n = town();
    n.settlements[1].discovered = false;
    const TravelPlan plan = planFastTravel(n, "a", "c");
    CHECK_FALSE(plan.allowed);
    CHECK(plan.refusal == TravelRefusal::NotDiscovered);
}

TEST_CASE("a cut road removes the link until it is reopened", "[fast_travel]")
{
    TravelNetwork n = town();
    n.links[1].secured = false;
    CHECK(planFastTravel(n, "a", "c").refusal == TravelRefusal::RouteCut);
    CHECK(planFastTravel(n, "a", "d").allowed);
    CHECK(fastTravelDestinations(n, "a") == Path{"b", "d"});
    n.links[1].secured = true;
    CHECK(planFastTravel(n, "a", "c").allowed);
    CHECK(fastTravelDestinations(n, "a") == Path{"b", "c", "d"});
}

TEST_CASE("a cut road is routed around when another secured route exists", "[fast_travel]")
{
    TravelNetwork n = town();
    n.links.push_back({"a", "c", 100, true});
    n.links[0].secured = false;
    const TravelPlan plan = planFastTravel(n, "a", "c");
    REQUIRE(plan.allowed);
    CHECK(plan.path == Path{"a", "c"});
    CHECK(plan.totalLength == 100);
}

TEST_CASE("the shortest route wins and ties go to the smaller id", "[fast_travel]")
{
    TravelNetwork n;
    n.settlements = {{"s", true}, {"x", true}, {"y", true}, {"t", true}};
    n.links = {{"s", "y", 5, true}, {"y", "t", 5, true}, {"s", "x", 5, true}, {"x", "t", 5, true}};
    CHECK(planFastTravel(n, "s", "t").path == Path{"s", "x", "t"});
    std::reverse(n.links.begin(), n.links.end());
    std::reverse(n.settlements.begin(), n.settlements.end());
    CHECK(planFastTravel(n, "s", "t").path == Path{"s", "x", "t"});
    n.links.push_back({"s", "t", 11, true});
    CHECK(planFastTravel(n, "s", "t").totalLength == 10);
    n.links.push_back({"s", "t", 9, true});
    CHECK(planFastTravel(n, "s", "t").path == Path{"s", "t"});
}

TEST_CASE("a tie is broken by id even when the smaller id settles later", "[fast_travel]")
{
    TravelNetwork n;
    n.settlements = {{"s", true}, {"a", true}, {"b", true}, {"t", true}};
    n.links = {{"s", "b", 1, true}, {"b", "t", 9, true}, {"s", "a", 5, true}, {"a", "t", 5, true}};
    const TravelPlan plan = planFastTravel(n, "s", "t");
    CHECK(plan.totalLength == 10);
    CHECK(plan.path == Path{"s", "a", "t"});
    std::reverse(n.links.begin(), n.links.end());
    std::reverse(n.settlements.begin(), n.settlements.end());
    CHECK(planFastTravel(n, "s", "t").path == Path{"s", "a", "t"});
}

TEST_CASE("zero length links and long chains are handled", "[fast_travel]")
{
    TravelNetwork n;
    n.settlements = {{"a", true}, {"b", true}, {"c", true}};
    n.links = {{"a", "b", 0, true}, {"b", "c", 0, true}, {"c", "a", 0, true}};
    CHECK(planFastTravel(n, "a", "c").path == Path{"a", "c"});

    TravelNetwork chain;
    for (int i = 0; i < 2000; ++i) {
        chain.settlements.push_back({"n" + std::to_string(i), true});
        if (i > 0) {
            chain.links.push_back({"n" + std::to_string(i - 1), "n" + std::to_string(i), 1, true});
        }
    }
    const TravelPlan plan = planFastTravel(chain, "n0", "n1999");
    REQUIRE(plan.allowed);
    CHECK(plan.path.size() == 2000);
    CHECK(plan.totalLength == 1999);
}

TEST_CASE("an undiscovered start with no road is refused as undiscovered", "[fast_travel]")
{
    TravelNetwork n = town();
    n.settlements[4].discovered = false;
    CHECK(planFastTravel(n, "e", "a").refusal == TravelRefusal::NotDiscovered);
}

TEST_CASE("a shorter cut duplicate does not override a secured link", "[fast_travel]")
{
    TravelNetwork n;
    n.settlements = {{"a", true}, {"b", true}};
    n.links = {{"a", "b", 1, false}, {"a", "b", 7, true}, {"b", "a", 3, true}};
    CHECK(planFastTravel(n, "a", "b").totalLength == 3);
}

TEST_CASE("unknown ids, same settlement and disconnected settlements", "[fast_travel]")
{
    const TravelNetwork n = town();
    CHECK(planFastTravel(n, "a", "zz").refusal == TravelRefusal::UnknownSettlement);
    CHECK(planFastTravel(n, "zz", "a").refusal == TravelRefusal::UnknownSettlement);
    CHECK(planFastTravel(n, "", "").refusal == TravelRefusal::UnknownSettlement);
    CHECK(planFastTravel(n, "a", "a").refusal == TravelRefusal::SameSettlement);
    CHECK(planFastTravel(n, "a", "e").refusal == TravelRefusal::NoRoad);
    CHECK(planFastTravel(TravelNetwork{}, "a", "b").refusal == TravelRefusal::UnknownSettlement);
}

TEST_CASE("hostile networks are tolerated", "[fast_travel]")
{
    TravelNetwork n = town();
    n.links.push_back({"a", "a", 1, true});
    n.links.push_back({"a", "ghost", 1, true});
    n.links.push_back({"ghost", "ghost2", 1, true});
    n.settlements.push_back({"a", false});
    CHECK(planFastTravel(n, "a", "c").allowed);
    CHECK(fastTravelDestinations(n, "a") == Path{"b", "c", "d"});
    CHECK(fastTravelDestinations(n, "ghost").empty());
}

TEST_CASE("extreme link lengths do not overflow", "[fast_travel]")
{
    TravelNetwork n;
    n.settlements = {{"a", true}, {"b", true}, {"c", true}};
    const auto big = std::numeric_limits<std::uint32_t>::max();
    n.links = {{"a", "b", big, true}, {"b", "c", big, true}};
    const TravelPlan plan = planFastTravel(n, "a", "c");
    REQUIRE(plan.allowed);
    CHECK(plan.totalLength == 2ull * big);
}

TEST_CASE("destinations exclude undiscovered starts and unreachable settlements", "[fast_travel]")
{
    TravelNetwork n = town();
    CHECK(fastTravelDestinations(n, "a") == Path{"b", "c", "d"});
    CHECK(fastTravelDestinations(n, "e").empty());
    n.settlements[0].discovered = false;
    CHECK(fastTravelDestinations(n, "a").empty());
    CHECK(fastTravelDestinations(n, "b") == Path{"c", "d"});
}
