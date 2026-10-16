#include "ichor/ui/waypoint_set.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kPi = 3.14159265358979f;
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

Waypoint wp(const std::string& id, float x, float y)
{
    return Waypoint{id, {x, y}};
}

WaypointSettings on()
{
    WaypointSettings s;
    s.enabled = true;
    return s;
}

} // namespace

TEST_CASE("waypoints are off by default", "[waypoint]")
{
    CHECK_FALSE(WaypointSettings{}.enabled);
}

TEST_CASE("off returns no data however many waypoints exist", "[waypoint]")
{
    WaypointSet set;
    REQUIRE(set.add(wp("a", 0.0f, 100.0f)) == WaypointResult::Ok);
    REQUIRE(set.add(wp("b", 50.0f, 0.0f)) == WaypointResult::Ok);

    CHECK_FALSE(set.guide(WaypointSettings{}, {0.0f, 0.0f}, 0.0f).has_value());
    // The same set answers once the flag is on, so the empty answer above is the flag's doing.
    CHECK(set.guide(on(), {0.0f, 0.0f}, 0.0f).has_value());

    WaypointSettings off = on();
    off.enabled = false;
    CHECK_FALSE(set.guide(off, {0.0f, 0.0f}, 0.0f).has_value());
}

TEST_CASE("on with no waypoints returns nothing", "[waypoint]")
{
    WaypointSet set;
    CHECK_FALSE(set.guide(on(), {0.0f, 0.0f}, 0.0f).has_value());
}

TEST_CASE("on with one waypoint gives bearing and distance", "[waypoint]")
{
    WaypointSet set;
    REQUIRE(set.add(wp("north", 0.0f, 100.0f)) == WaypointResult::Ok);
    auto g = set.guide(on(), {0.0f, 0.0f}, 0.0f);
    REQUIRE(g.has_value());
    CHECK(g->id == "north");
    CHECK(g->distance == Approx(100.0f));
    CHECK(g->bearing == Approx(0.0f).margin(1e-4));
    CHECK(g->relativeBearing == Approx(0.0f).margin(1e-4));

    SECTION("east is a quarter turn clockwise")
    {
        WaypointSet east;
        REQUIRE(east.add(wp("e", 30.0f, 0.0f)) == WaypointResult::Ok);
        auto ge = east.guide(on(), {0.0f, 0.0f}, 0.0f);
        REQUIRE(ge.has_value());
        CHECK(ge->bearing == Approx(kPi / 2.0f).margin(1e-3));
        CHECK(ge->distance == Approx(30.0f));
    }
}

TEST_CASE("many waypoints: the active one is reported and can change", "[waypoint]")
{
    WaypointSet set;
    REQUIRE(set.add(wp("a", 10.0f, 0.0f)) == WaypointResult::Ok);
    REQUIRE(set.add(wp("b", 0.0f, 20.0f)) == WaypointResult::Ok);
    REQUIRE(set.add(wp("c", -30.0f, 0.0f)) == WaypointResult::Ok);

    CHECK(set.guide(on(), {0.0f, 0.0f}, 0.0f)->id == "a");
    REQUIRE(set.setActive("c") == WaypointResult::Ok);
    auto g = set.guide(on(), {0.0f, 0.0f}, 0.0f);
    REQUIRE(g.has_value());
    CHECK(g->id == "c");
    CHECK(g->distance == Approx(30.0f));
    CHECK(set.setActive("nope") == WaypointResult::UnknownId);
    CHECK(set.guide(on(), {0.0f, 0.0f}, 0.0f)->id == "c");

    REQUIRE(set.remove("c") == WaypointResult::Ok);
    CHECK(set.guide(on(), {0.0f, 0.0f}, 0.0f)->id == "a");
    REQUIRE(set.remove("a") == WaypointResult::Ok);
    CHECK(set.guide(on(), {0.0f, 0.0f}, 0.0f)->id == "b");
    REQUIRE(set.remove("b") == WaypointResult::Ok);
    CHECK_FALSE(set.guide(on(), {0.0f, 0.0f}, 0.0f).has_value());
    CHECK(set.remove("b") == WaypointResult::UnknownId);
}

TEST_CASE("removing a waypoint before the active one keeps the active one", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("a", 1.0f, 0.0f));
    set.add(wp("b", 2.0f, 0.0f));
    set.add(wp("c", 3.0f, 0.0f));
    set.setActive("c");
    REQUIRE(set.remove("a") == WaypointResult::Ok);
    CHECK(set.active()->id == "c");
}

TEST_CASE("the cap is enforced", "[waypoint]")
{
    WaypointLimits limits;
    limits.maxWaypoints = 3;
    WaypointSet set(limits);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(set.add(wp("w" + std::to_string(i), 1.0f, 1.0f)) == WaypointResult::Ok);
    }
    CHECK(set.add(wp("extra", 1.0f, 1.0f)) == WaypointResult::TooMany);
    CHECK(set.size() == 3);
    CHECK(set.find("extra") == nullptr);
    REQUIRE(set.remove("w1") == WaypointResult::Ok);
    CHECK(set.add(wp("extra", 1.0f, 1.0f)) == WaypointResult::Ok);
}

TEST_CASE("ids are validated and unique", "[waypoint]")
{
    WaypointSet set;
    CHECK(set.add(wp("", 0.0f, 0.0f)) == WaypointResult::InvalidId);
    CHECK(set.add(wp("has space", 0.0f, 0.0f)) == WaypointResult::InvalidId);
    CHECK(set.add(wp(std::string(49, 'a'), 0.0f, 0.0f)) == WaypointResult::InvalidId);
    CHECK(set.add(wp(std::string("a\0b", 3), 0.0f, 0.0f)) == WaypointResult::InvalidId);
    REQUIRE(set.add(wp("ok", 1.0f, 1.0f)) == WaypointResult::Ok);
    CHECK(set.add(wp("ok", 2.0f, 2.0f)) == WaypointResult::DuplicateId);
    CHECK(set.size() == 1);
    CHECK(set.find("ok")->position.x == 1.0f);
}

TEST_CASE("hostile waypoint positions are rejected", "[waypoint]")
{
    WaypointSet set;
    CHECK(set.add(wp("nan", kNaN, 0.0f)) == WaypointResult::InvalidPosition);
    CHECK(set.add(wp("inf", 0.0f, kInf)) == WaypointResult::InvalidPosition);
    CHECK(set.add(wp("ninf", -kInf, 0.0f)) == WaypointResult::InvalidPosition);
    CHECK(set.add(wp("far", 2.0e6f, 0.0f)) == WaypointResult::InvalidPosition);
    CHECK(set.size() == 0);
    CHECK(set.add(wp("edge", 1.0e6f, -1.0e6f)) == WaypointResult::Ok);
}

TEST_CASE("hostile player position or heading yields nothing, not garbage", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("a", 10.0f, 10.0f));
    CHECK_FALSE(set.guide(on(), {kNaN, 0.0f}, 0.0f).has_value());
    CHECK_FALSE(set.guide(on(), {0.0f, kInf}, 0.0f).has_value());
    CHECK_FALSE(set.guide(on(), {5.0e6f, 0.0f}, 0.0f).has_value());
    CHECK_FALSE(set.guide(on(), {0.0f, 0.0f}, kNaN).has_value());
    CHECK_FALSE(set.guide(on(), {0.0f, 0.0f}, kInf).has_value());
}

TEST_CASE("extreme but valid positions stay finite", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("far", 1.0e6f, 1.0e6f));
    auto g = set.guide(on(), {-1.0e6f, -1.0e6f}, 0.0f);
    REQUIRE(g.has_value());
    CHECK(std::isfinite(g->distance));
    CHECK(g->distance == Approx(2.0e6f * std::sqrt(2.0f)).epsilon(1e-5));
    CHECK(g->bearing == Approx(kPi / 4.0f).epsilon(1e-4));
}

TEST_CASE("standing on the waypoint gives distance zero and a finite bearing", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("here", 5.0f, 5.0f));
    auto g = set.guide(on(), {5.0f, 5.0f}, 1.0f);
    REQUIRE(g.has_value());
    CHECK(g->distance == 0.0f);
    CHECK(std::isfinite(g->bearing));
    CHECK(std::isfinite(g->relativeBearing));
}

TEST_CASE("bearing wraps around north and stays in range", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("ne", 10.0f, 10.0f)); // bearing pi/4

    SECTION("relative bearing takes the short way round")
    {
        auto g = set.guide(on(), {0.0f, 0.0f}, 7.0f * kPi / 4.0f);
        REQUIRE(g.has_value());
        CHECK(g->relativeBearing == Approx(kPi / 2.0f).margin(1e-3));
        auto h = set.guide(on(), {0.0f, 0.0f}, 3.0f * kPi / 4.0f);
        REQUIRE(h.has_value());
        CHECK(h->relativeBearing == Approx(-kPi / 2.0f).margin(1e-3));
    }

    SECTION("headings outside one turn are normalised")
    {
        auto a = set.guide(on(), {0.0f, 0.0f}, 0.5f);
        auto b = set.guide(on(), {0.0f, 0.0f}, 0.5f + 4.0f * kPi);
        auto c = set.guide(on(), {0.0f, 0.0f}, 0.5f - 6.0f * kPi);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(c.has_value());
        CHECK(b->relativeBearing == Approx(a->relativeBearing).margin(1e-3));
        CHECK(c->relativeBearing == Approx(a->relativeBearing).margin(1e-3));
    }

    SECTION("directly behind is +pi, never -pi")
    {
        WaypointSet south;
        south.add(wp("s", 0.0f, -10.0f));
        auto g = south.guide(on(), {0.0f, 0.0f}, 0.0f);
        REQUIRE(g.has_value());
        CHECK(g->relativeBearing == Approx(kPi).margin(1e-3));
        CHECK(g->relativeBearing <= kPi);
        CHECK(g->relativeBearing > -kPi);
    }

    SECTION("west is bearing 3pi/2, inside [0, 2pi)")
    {
        WaypointSet west;
        west.add(wp("w", -10.0f, 0.0f));
        auto g = west.guide(on(), {0.0f, 0.0f}, 0.0f);
        REQUIRE(g.has_value());
        CHECK(g->bearing == Approx(3.0f * kPi / 2.0f).margin(1e-3));
        CHECK(g->bearing >= 0.0f);
        CHECK(g->bearing < 2.0f * kPi);
    }
}

TEST_CASE("rejected calls leave the active waypoint alone", "[waypoint]")
{
    WaypointSet set;
    set.add(wp("a", 1.0f, 0.0f));
    set.add(wp("b", 2.0f, 0.0f));
    set.setActive("b");
    set.add(wp("a", 9.0f, 9.0f));
    set.add(wp("bad", kNaN, 0.0f));
    CHECK(set.active()->id == "b");
}
