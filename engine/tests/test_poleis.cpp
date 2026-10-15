#include "ichor/factions/poleis.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <numeric>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::uint32_t total(const IncumbentScaling& s)
{
    return std::accumulate(s.shares.begin(), s.shares.end(), 0u, [](std::uint32_t a, const IncumbentShare& b) { return a + b.scale; });
}

}

TEST_CASE("a single-power world gives its one Incumbent the full scale", "[factions][poleis]")
{
    const auto s = scaleIncumbentsForAxioms({"powers.hegemony"}, 3, 100);
    REQUIRE(s.has_value());
    CHECK(s->shape == IncumbentShape::Single);
    REQUIRE(s->shares.size() == 1);
    CHECK(s->shares[0].id == "archonate");
    CHECK(s->shares[0].scale == 100);
    CHECK(s->wars.empty());
}

TEST_CASE("the Poleis have no single Incumbent: scale is split and neighbours are at war", "[factions][poleis]")
{
    const auto s = scaleIncumbentsForAxioms({"powers.poleis"}, 4, 100);
    REQUIRE(s.has_value());
    CHECK(s->shape == IncumbentShape::Poleis);
    REQUIRE(s->shares.size() == 4);
    for (const auto& share : s->shares) {
        CHECK(share.scale == 25);
    }
    REQUIRE(s->wars.size() == 3);
    CHECK(s->wars[0] == std::pair<std::string, std::string>("polis_1", "polis_2"));
    CHECK(s->wars[2] == std::pair<std::string, std::string>("polis_3", "polis_4"));
}

TEST_CASE("an uneven split gives the remainder to the first polis and sums exactly", "[factions][poleis]")
{
    const auto s = scaleIncumbentsForAxioms({"powers.poleis"}, 3, 100);
    REQUIRE(s.has_value());
    CHECK(s->shares[0].scale == 34);
    CHECK(s->shares[1].scale == 33);
    CHECK(s->shares[2].scale == 33);
    CHECK(total(*s) == 100);
    CHECK(s->wars.size() == 2);
    const auto zero = scaleIncumbentsForAxioms({"powers.poleis"}, 3, 0);
    REQUIRE(zero.has_value());
    CHECK(total(*zero) == 0);
}

TEST_CASE("the Dark Age has no shares and no wars", "[factions][poleis]")
{
    const auto s = scaleIncumbentsForAxioms({"powers.dark_age"}, 3, 100);
    REQUIRE(s.has_value());
    CHECK(s->shape == IncumbentShape::None);
    CHECK(s->shares.empty());
    CHECK(s->wars.empty());
}

TEST_CASE("only the Powers axiom matters among a mixed axiom set", "[factions][poleis]")
{
    const auto s = scaleIncumbentsForAxioms({"law.orichalcum", "powers.poleis", "cataclysm.flood", "pressure.famine"}, 3, 90);
    REQUIRE(s.has_value());
    CHECK(s->shape == IncumbentShape::Poleis);
    CHECK(total(*s) == 90);
}

TEST_CASE("two Powers axioms, none, or an empty set give no scaling", "[factions][poleis]")
{
    CHECK_FALSE(scaleIncumbentsForAxioms({"powers.poleis", "powers.league"}, 3, 100).has_value());
    CHECK_FALSE(scaleIncumbentsForAxioms({"powers.poleis", "powers.poleis"}, 3, 100).has_value());
    CHECK_FALSE(scaleIncumbentsForAxioms({"law.orichalcum"}, 3, 100).has_value());
    CHECK_FALSE(scaleIncumbentsForAxioms({}, 3, 100).has_value());
}

TEST_CASE("hostile ids and counts are rejected without throwing", "[factions][poleis]")
{
    for (const char* id : {"", "powers.", "powers.unknown", "Powers.poleis", " powers.poleis", "powers.poleis "}) {
        CHECK_FALSE(scaleIncumbentsForAxioms({id}, 3, 100).has_value());
    }
    CHECK_FALSE(scaleIncumbentsForAxioms({std::string("powers.poleis\0x", 15)}, 3, 100).has_value());
    CHECK_FALSE(scaleIncumbentsForAxioms({"powers." + std::string(1 << 20, 'a')}, 3, 100).has_value());
    for (const int count : {std::numeric_limits<int>::min(), -1, 0, 2, 5, std::numeric_limits<int>::max()}) {
        CHECK_FALSE(scaleIncumbentsForAxioms({"powers.poleis"}, count, 100).has_value());
    }
    CHECK_FALSE(scaleIncumbentsForAxioms({"powers.poleis"}, 3, kMaxIncumbentScale + 1).has_value());
    CHECK_FALSE(scaleIncumbentsForAxioms({"powers.poleis"}, 3, std::numeric_limits<std::uint32_t>::max()).has_value());
    CHECK(scaleIncumbentsForAxioms({"powers.poleis"}, 3, kMaxIncumbentScale).has_value());
}
