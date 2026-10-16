#include "ichor/factions/contested.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

TerritoryResult territory(std::vector<std::string> ids, std::size_t cells)
{
    TerritoryResult t;
    t.ids = std::move(ids);
    t.strength.assign(t.ids.size(), std::vector<std::uint16_t>(cells, 0));
    return t;
}

HostilityEntry entry(const char* a, const char* b, std::int32_t hostility)
{
    HostilityEntry e;
    e.a = a;
    e.b = b;
    e.hostility = hostility;
    e.relation = hostility >= kHostileAtLeast ? Relation::Hostile : (hostility <= kAllyAtMost ? Relation::Allied : Relation::Neutral);
    return e;
}

ContestedResult run(std::uint32_t w, std::uint32_t h, const TerritoryResult& t, const std::vector<HostilityEntry>& m, const std::vector<std::uint8_t>& value = {},
    std::size_t maxResults = kDefaultMaxContested)
{
    ContestedResult out;
    std::vector<std::string> problems;
    REQUIRE(findContestedCells(w, h, t, m, value, maxResults, out, problems));
    REQUIRE(problems.empty());
    return out;
}

}

TEST_CASE("disjoint non-hostile claims have no contested cells", "[contested]")
{
    auto t = territory({"a", "b"}, 6);
    t.strength[0][0] = t.strength[0][1] = 100;
    t.strength[1][4] = t.strength[1][5] = 100;
    auto r = run(6, 1, t, {entry("a", "b", 300)});
    CHECK(r.cells.empty());
    CHECK(r.total == 0);
}

TEST_CASE("empty claims and empty maps are fine", "[contested]")
{
    CHECK(run(4, 4, territory({"a", "b"}, 16), {}).total == 0);
    CHECK(run(0, 0, territory({}, 0), {}).total == 0);
    CHECK(run(3, 3, territory({}, 9), {}).total == 0);
}

TEST_CASE("an overlapped cell is found with its claimants and hostility", "[contested]")
{
    auto t = territory({"a", "b"}, 5);
    t.strength[0][2] = 300;
    t.strength[1][2] = 200;
    t.strength[0][1] = 50;
    auto r = run(5, 1, t, {entry("a", "b", 400)});
    REQUIRE(r.cells.size() == 1);
    CHECK(r.cells[0].cell == 2);
    CHECK(r.cells[0].kind == ContestedKind::Overlap);
    CHECK(r.cells[0].claimants == 0b11);
    CHECK(r.cells[0].claimantCount == 2);
    CHECK(r.cells[0].hostility == 400);
    CHECK(r.cells[0].strength == 500);
}

TEST_CASE("many-way overlap takes the worst pair and counts every claimant", "[contested]")
{
    auto t = territory({"a", "b", "c", "d"}, 3);
    for (auto& row : t.strength)
        row[1] = 10;
    auto r = run(3, 1, t, {entry("a", "b", 100), entry("b", "c", 900), entry("a", "d", 500)});
    REQUIRE(r.cells.size() == 1);
    CHECK(r.cells[0].claimantCount == 4);
    CHECK(r.cells[0].hostility == 900);
}

TEST_CASE("a cell next to a hostile claim is a border, one next to a non-hostile claim is not", "[contested]")
{
    auto t = territory({"a", "b", "c"}, 6);
    t.strength[0][0] = t.strength[0][1] = 100;
    t.strength[1][2] = 100;
    t.strength[2][4] = t.strength[2][5] = 100;
    auto r = run(6, 1, t, {entry("a", "b", 1200), entry("b", "c", 300), entry("a", "c", 100)});
    REQUIRE(r.cells.size() == 2);
    CHECK(r.borderTotal == 2);
    CHECK(r.overlapTotal == 0);
    for (const auto& c : r.cells) {
        CHECK(c.kind == ContestedKind::Border);
        CHECK(c.hostility == 1200);
    }
    CHECK((r.cells[0].cell == 1 || r.cells[1].cell == 1));
    CHECK((r.cells[0].cell == 2 || r.cells[1].cell == 2));
}

TEST_CASE("border adjacency is 4-neighbour and does not wrap rows", "[contested]")
{
    auto t = territory({"a", "b"}, 9);
    t.strength[0][2] = 100;  // (2,0)
    t.strength[1][3] = 100;  // (0,1): diagonal-distance across the row wrap, not adjacent
    t.strength[1][5] = 100;  // (2,1): directly below a
    auto r = run(3, 3, t, {entry("a", "b", 1500)});
    std::vector<std::uint32_t> cells;
    for (const auto& c : r.cells)
        cells.push_back(c.cell);
    CHECK(cells == std::vector<std::uint32_t>{2, 5});
}

TEST_CASE("ranking: hostility, then value, then claimants, then cell index", "[contested]")
{
    auto t = territory({"a", "b", "c"}, 8);
    for (std::size_t c : {0u, 1u, 2u, 3u}) {
        t.strength[0][c] = 100;
        t.strength[1][c] = 100;
    }
    t.strength[2][3] = 100;
    t.strength[0][7] = t.strength[1][7] = 100;
    const std::vector<HostilityEntry> m{entry("a", "b", 500), entry("a", "c", 500), entry("b", "c", 500)};
    const std::vector<std::uint8_t> value{5, 5, 9, 1, 0, 0, 0, 5};
    auto r = run(8, 1, t, m, value);
    std::vector<std::uint32_t> order;
    for (const auto& c : r.cells)
        order.push_back(c.cell);
    // value 9 first, then value 5 (cells 0, 1, 7 tie: cell index), then cell 3 (value 1)
    CHECK(order == std::vector<std::uint32_t>{2, 0, 1, 7, 3});

    auto hot = m;
    hot[0].hostility = 800;
    auto r2 = run(8, 1, t, hot, value);
    CHECK(r2.cells.front().cell == 2);
    CHECK(r2.cells.front().hostility == 800);
}

TEST_CASE("equal cells order by claimant count, strength, then index", "[contested]")
{
    auto t = territory({"a", "b", "c"}, 4);
    t.strength[0][0] = t.strength[1][0] = 100;
    t.strength[0][1] = t.strength[1][1] = 200;
    t.strength[0][2] = t.strength[1][2] = t.strength[2][2] = 50;
    t.strength[0][3] = t.strength[1][3] = 200;
    auto r = run(4, 1, t, {});
    std::vector<std::uint32_t> order;
    for (const auto& c : r.cells)
        order.push_back(c.cell);
    CHECK(order == std::vector<std::uint32_t>{2, 1, 3, 0});
}

TEST_CASE("the result is capped at maxResults but keeps the true total and the best cells", "[contested]")
{
    auto t = territory({"a", "b"}, 100);
    std::vector<std::uint8_t> value(100);
    for (std::size_t c = 0; c < 100; ++c) {
        t.strength[0][c] = t.strength[1][c] = 10;
        value[c] = static_cast<std::uint8_t>(c);
    }
    auto r = run(100, 1, t, {}, value, 3);
    REQUIRE(r.cells.size() == 3);
    CHECK(r.total == 100);
    CHECK(r.cells[0].cell == 99);
    CHECK(r.cells[1].cell == 98);
    CHECK(r.cells[2].cell == 97);
    CHECK(run(100, 1, t, {}, value, 0).cells.empty());
}

TEST_CASE("a huge map stays bounded and deterministic", "[contested]")
{
    const std::uint32_t w = 1024;
    const std::uint32_t h = 1024;
    auto t = territory({"a", "b", "c"}, static_cast<std::size_t>(w) * h);
    for (std::size_t c = 0; c < t.strength[0].size(); ++c) {
        t.strength[0][c] = (c % 3 != 0) ? 100 : 0;
        t.strength[1][c] = (c % 2 == 0) ? 100 : 0;
        t.strength[2][c] = (c % 5 == 0) ? 100 : 0;
    }
    const std::vector<HostilityEntry> m{entry("a", "b", 700), entry("a", "c", 1300), entry("b", "c", 200)};
    auto r1 = run(w, h, t, m, {}, 64);
    auto r2 = run(w, h, t, m, {}, 64);
    CHECK(r1.cells.size() == 64);
    CHECK(r1.total > 64);
    CHECK(r1.cells.front().hostility >= r1.cells.back().hostility);
    CHECK(contestedDigest(r1) == contestedDigest(r2));
}

TEST_CASE("unknown and self ids in the matrix are ignored", "[contested]")
{
    auto t = territory({"a", "b"}, 3);
    t.strength[0][1] = t.strength[1][1] = 10;
    auto r = run(3, 1, t, {entry("a", "zzz", 1900), entry("a", "a", 1900), entry("", "", 1900), entry("a", "b", 600)});
    REQUIRE(r.cells.size() == 1);
    CHECK(r.cells[0].hostility == 600);
}

TEST_CASE("a pair missing from the matrix counts as zero hostility", "[contested]")
{
    auto t = territory({"a", "b"}, 3);
    t.strength[0][1] = t.strength[1][1] = 10;
    auto r = run(3, 1, t, {});
    REQUIRE(r.cells.size() == 1);
    CHECK(r.cells[0].hostility == 0);
}

TEST_CASE("malformed input is rejected without crashing", "[contested]")
{
    ContestedResult out;
    std::vector<std::string> problems;

    CHECK_FALSE(findContestedCells(4, 1, territory({"a", "a"}, 4), {}, {}, 10, out, problems));
    CHECK(!problems.empty());
    CHECK(out.cells.empty());

    problems.clear();
    CHECK_FALSE(findContestedCells(4, 1, territory({"a"}, 3), {}, {}, 10, out, problems));
    CHECK(!problems.empty());

    problems.clear();
    CHECK_FALSE(findContestedCells(4, 1, territory({"a"}, 4), {}, std::vector<std::uint8_t>(2), 10, out, problems));
    CHECK(!problems.empty());

    problems.clear();
    std::vector<std::string> many;
    for (std::size_t i = 0; i <= kMaxTerritoryFactions; ++i)
        many.push_back("f" + std::to_string(i));
    CHECK_FALSE(findContestedCells(2, 1, territory(many, 2), {}, {}, 10, out, problems));

    problems.clear();
    auto lopsided = territory({"a", "b"}, 4);
    lopsided.strength.pop_back();
    CHECK_FALSE(findContestedCells(4, 1, lopsided, {}, {}, 10, out, problems));

    problems.clear();
    CHECK_FALSE(findContestedCells(0xFFFFFFFFu, 0xFFFFFFFFu, territory({"a"}, 0), {}, {}, 10, out, problems));
    CHECK(!problems.empty());
    CHECK(problems.front().rfind("contested: ", 0) == 0);
}

TEST_CASE("the result does not depend on faction input order", "[contested]")
{
    auto t1 = territory({"a", "b"}, 4);
    t1.strength[0][1] = t1.strength[1][1] = 10;
    t1.strength[0][2] = 10;
    t1.strength[1][3] = 10;
    auto t2 = territory({"b", "a"}, 4);
    t2.strength[1] = t1.strength[0];
    t2.strength[0] = t1.strength[1];
    const std::vector<HostilityEntry> m{entry("a", "b", 1100)};
    auto r1 = run(4, 1, t1, m);
    auto r2 = run(4, 1, t2, m);
    REQUIRE(r1.cells.size() == r2.cells.size());
    for (std::size_t i = 0; i < r1.cells.size(); ++i) {
        CHECK(r1.cells[i].cell == r2.cells[i].cell);
        CHECK(r1.cells[i].hostility == r2.cells[i].hostility);
        CHECK(r1.cells[i].claimantCount == r2.cells[i].claimantCount);
    }
}
