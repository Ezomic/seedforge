#include "ichor/graph/shaft_sightline.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

using namespace ichor::graph::shaft;

namespace {

Room room(std::uint32_t id, std::int32_t floor, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1)
{
    return Room{id, floor, x0, y0, x1, y1};
}

struct Tower {
    std::vector<Room> rooms;
    std::vector<Link> links;
};

// Rooms 1..count in a chain, cycling through `floors` floors on one footprint.
Tower tower(std::uint32_t count, std::int32_t floors)
{
    Tower t;
    for (std::uint32_t i = 1; i <= count; ++i) {
        t.rooms.push_back(room(i, static_cast<std::int32_t>((i - 1) % static_cast<std::uint32_t>(floors)), 0, 0, 6, 6));
        if (i > 1) t.links.push_back({i - 1, i});
    }
    return t;
}

Params params(std::uint64_t seed = 1, std::uint32_t maxShafts = 4)
{
    Params p;
    p.seed = seed;
    p.maxShafts = maxShafts;
    p.minWalkDistance = 1;
    return p;
}

bool inRoom(const Room& r, std::int32_t x, std::int32_t y)
{
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

auto key(const Shaft& s)
{
    return std::make_tuple(s.upper, s.lower, s.x, s.y);
}

}

TEST_CASE("empty and single-floor input yields no shafts", "[shaft]")
{
    CHECK(chooseShafts({}, {}, {}, params()).empty());
    std::vector<Room> flat{room(1, 0, 0, 0, 5, 5), room(2, 0, 0, 0, 5, 5), room(3, 0, 6, 0, 9, 5)};
    CHECK(chooseShafts(flat, {{1, 2}, {2, 3}}, {}, params()).empty());
}

TEST_CASE("stacked rooms far apart on foot get a shaft inside both footprints", "[shaft]")
{
    Tower t = tower(5, 2);
    Params p = params(7, 8);
    p.minWalkDistance = 3;
    auto shafts = chooseShafts(t.rooms, t.links, {}, p);
    REQUIRE_FALSE(shafts.empty());
    for (const Shaft& s : shafts) {
        CHECK(s.upperFloor > s.lowerFloor);
        CHECK(s.walkDistance >= 3);
        const Room* up = nullptr;
        const Room* lo = nullptr;
        for (const Room& r : t.rooms) {
            if (r.id == s.upper) up = &r;
            if (r.id == s.lower) lo = &r;
        }
        REQUIRE(up);
        REQUIRE(lo);
        CHECK(inRoom(*up, s.x, s.y));
        CHECK(inRoom(*lo, s.x, s.y));
    }
}

TEST_CASE("the cap bounds the count and zero means none", "[shaft]")
{
    Tower t = tower(40, 2);
    CHECK(chooseShafts(t.rooms, t.links, {}, params(3, 0)).empty());
    CHECK(chooseShafts(t.rooms, t.links, {}, params(3, 2)).size() == 2);
    CHECK(chooseShafts(t.rooms, t.links, {}, params(3, 1000000)).size() <= kMaxShafts);
}

TEST_CASE("repeated runs are identical and the seed matters", "[shaft]")
{
    Tower t = tower(30, 3);
    auto a = chooseShafts(t.rooms, t.links, {}, params(11, 6));
    auto b = chooseShafts(t.rooms, t.links, {}, params(11, 6));
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(key(a[i]) == key(b[i]));

    bool differs = false;
    for (std::uint64_t seed = 12; seed < 60 && !differs; ++seed) {
        auto c = chooseShafts(t.rooms, t.links, {}, params(seed, 6));
        differs = c.size() != a.size() || !std::equal(a.begin(), a.end(), c.begin(), [](const Shaft& l, const Shaft& r) { return key(l) == key(r); });
    }
    CHECK(differs);
}

TEST_CASE("input order does not change the result", "[shaft]")
{
    Tower t = tower(12, 3);
    auto a = chooseShafts(t.rooms, t.links, {}, params(5, 4));
    std::reverse(t.rooms.begin(), t.rooms.end());
    std::reverse(t.links.begin(), t.links.end());
    auto b = chooseShafts(t.rooms, t.links, {}, params(5, 4));
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(key(a[i]) == key(b[i]));
}

TEST_CASE("a shaft never opens a reserved cell of the upper floor", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 3, 1), room(2, 1, 0, 0, 3, 1)};
    std::vector<Link> links{{1, 2}};
    std::vector<Cell> reserved{{1, 0, 0}, {1, 1, 0}};
    auto shafts = chooseShafts(rooms, links, reserved, params(2));
    REQUIRE(shafts.size() == 1);
    CHECK(shafts[0].x == 2);

    reserved.push_back({1, 2, 0});
    CHECK(chooseShafts(rooms, links, reserved, params(2)).empty());

    // A reserved cell on the lower floor does not matter: nobody cuts it.
    CHECK(chooseShafts(rooms, links, {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}}, params(2)).size() == 1);
}

TEST_CASE("a shaft never cuts through a room or reserved cell on a floor between", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 2, 1), room(2, 1, 0, 0, 2, 1), room(3, 2, 0, 0, 2, 1)};
    std::vector<Link> links{{1, 2}, {2, 3}};
    Params p = params(4, 8);
    p.minWalkDistance = 2;
    CHECK(chooseShafts(rooms, links, {}, p).empty());

    rooms[1] = room(2, 1, 5, 5, 6, 6);
    auto shafts = chooseShafts(rooms, links, {}, p);
    REQUIRE(shafts.size() == 1);
    CHECK(shafts[0].upper == 3);
    CHECK(shafts[0].lower == 1);

    std::vector<Cell> reserved{{1, 0, 0}, {1, 1, 0}};
    CHECK(chooseShafts(rooms, links, reserved, p).empty());
}

TEST_CASE("maxSpan limits how many floors a shaft crosses", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 2, 2), room(2, 3, 0, 0, 2, 2)};
    std::vector<Link> links{{1, 2}};
    Params p = params(1);
    p.maxSpan = 2;
    CHECK(chooseShafts(rooms, links, {}, p).empty());
    p.maxSpan = 3;
    CHECK(chooseShafts(rooms, links, {}, p).size() == 1);
}

TEST_CASE("rooms on different floors without footprint overlap get nothing", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 3, 3), room(2, 1, 3, 0, 6, 3)};
    CHECK(chooseShafts(rooms, {{1, 2}}, {}, params()).empty());
}

TEST_CASE("each room is used once and shafts do not share a column", "[shaft]")
{
    Tower t = tower(40, 4);
    auto shafts = chooseShafts(t.rooms, t.links, {}, params(9, 20));
    std::set<std::uint32_t> used;
    std::set<std::tuple<std::int32_t, std::int32_t>> columns;
    for (const Shaft& s : shafts) {
        CHECK(used.insert(s.upper).second);
        CHECK(used.insert(s.lower).second);
        CHECK(columns.insert({s.x, s.y}).second);
    }
    CHECK(shafts.size() > 1);
}

TEST_CASE("farther pairs on foot win over closer ones", "[shaft]")
{
    // 1 (floor 0) and 2 (floor 1) are adjacent; 5 (floor 1) is three steps from 1.
    std::vector<Room> rooms{room(1, 0, 0, 0, 2, 2), room(2, 1, 0, 0, 2, 2), room(3, 2, 8, 8, 9, 9), room(4, 2, 8, 8, 9, 9), room(5, 1, 0, 0, 2, 2)};
    std::vector<Link> links{{1, 2}, {1, 3}, {3, 4}, {4, 5}};
    auto shafts = chooseShafts(rooms, links, {}, params(1, 1));
    REQUIRE(shafts.size() == 1);
    CHECK(shafts[0].walkDistance == 3);
    CHECK(shafts[0].lower == 1);
    CHECK(shafts[0].upper == 5);
}

TEST_CASE("pairs below the walk distance threshold are skipped", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 2, 2), room(2, 1, 0, 0, 2, 2)};
    Params p = params();
    p.minWalkDistance = 2;
    CHECK(chooseShafts(rooms, {{1, 2}}, {}, p).empty());
}

TEST_CASE("disconnected rooms are still eligible and report unreachable", "[shaft]")
{
    std::vector<Room> rooms{room(1, 0, 0, 0, 2, 2), room(2, 1, 0, 0, 2, 2)};
    auto shafts = chooseShafts(rooms, {}, {}, params());
    REQUIRE(shafts.size() == 1);
    CHECK(shafts[0].walkDistance == kUnreachable);
}

TEST_CASE("degenerate and hostile input is ignored without crashing", "[shaft]")
{
    constexpr auto big = std::numeric_limits<std::int32_t>::max();
    constexpr auto small = std::numeric_limits<std::int32_t>::min();
    std::vector<Room> rooms{
        room(1, 0, 0, 0, 0, 0),
        room(2, 1, 5, 5, 2, 2),
        room(3, 0, small, small, big, big),
        room(4, small, 0, 0, 3, 3),
        room(5, big, 0, 0, 3, 3),
        room(6, 0, 0, 0, 3, 3),
        room(6, 1, 0, 0, 3, 3),
        room(7, 1, 0, 0, 3, 3),
    };
    std::vector<Link> links{{1, 1}, {99, 100}, {6, 7}, {6, 6}};
    std::vector<Cell> reserved{{big, big, big}, {small, small, small}, {1, 0, 0}};
    Params p = params(~0ull, 0xFFFFFFFFu);
    p.maxSpan = 0xFFFFFFFFu;
    p.minWalkDistance = 0xFFFFFFFFu;
    CHECK(chooseShafts(rooms, links, reserved, p).size() <= kMaxShafts);
    p.minWalkDistance = 0;
    for (const Shaft& s : chooseShafts(rooms, links, reserved, p)) {
        CHECK(s.upperFloor > s.lowerFloor);
        for (std::uint32_t bad : {1u, 2u, 3u, 4u, 5u}) {
            CHECK(s.upper != bad);
            CHECK(s.lower != bad);
        }
    }
}

TEST_CASE("huge input stays bounded", "[shaft]")
{
    Tower t = tower(5000, 6);
    for (std::uint32_t i = 0; i < 50000; ++i) t.links.push_back({i, i + 1});
    std::vector<Cell> reserved;
    for (std::int32_t i = 0; i < 100000; ++i) reserved.push_back({i % 6, i % 6, i % 6});
    auto shafts = chooseShafts(t.rooms, t.links, reserved, params(1, 1000));
    CHECK(shafts.size() <= kMaxShafts);
}
