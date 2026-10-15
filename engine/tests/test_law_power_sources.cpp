#include "ichor/progression/law_power_sources.h"
#include "ichor/world/temenos_field.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();

}

TEST_CASE("growth is clamped to the campaign cap", "[law]")
{
    CHECK(cappedGrowthPermille(1000) == 1000);
    CHECK(cappedGrowthPermille(1500) == 1500);
    CHECK(cappedGrowthPermille(2000) == 2000);
    CHECK(cappedGrowthPermille(2001) == 2000);
    CHECK(cappedGrowthPermille(kI64Max) == 2000);
    CHECK(cappedGrowthPermille(0) == 1000);
    CHECK(cappedGrowthPermille(-5) == 1000);
    CHECK(cappedGrowthPermille(kI64Min) == 1000);
}

TEST_CASE("mysteries slot capacity grows with the cap and stays bounded", "[law][mysteries]")
{
    CHECK(mysteriesSlotCapacity(4, 1000) == 4);
    CHECK(mysteriesSlotCapacity(4, 1500) == 6);
    CHECK(mysteriesSlotCapacity(4, 2000) == 8);
    CHECK(mysteriesSlotCapacity(4, 999999) == 8);
    CHECK(mysteriesSlotCapacity(5, 1500) == 7);
    CHECK(mysteriesSlotCapacity(-3, 2000) == 0);
    CHECK(mysteriesSlotCapacity(0, 2000) == 0);
    CHECK(mysteriesSlotCapacity(std::numeric_limits<int>::max(), 2000) == static_cast<int>(kMaxRiteLoadout));
}

TEST_CASE("a rite is admitted once, within capacity and use limits", "[law][mysteries]")
{
    MysteriesLoadout l;
    CHECK(mysteriesAdmit(l, "eleusis", 3, 2));
    CHECK_FALSE(mysteriesAdmit(l, "eleusis", 3, 2));
    CHECK_FALSE(mysteriesAdmit(l, "", 3, 2));
    CHECK_FALSE(mysteriesAdmit(l, "a", 0, 2));
    CHECK_FALSE(mysteriesAdmit(l, "a", -1, 2));
    CHECK_FALSE(mysteriesAdmit(l, "a", kMaxRiteUses + 1, 2));
    CHECK(mysteriesAdmit(l, "orphic", kMaxRiteUses, 2));
    CHECK_FALSE(mysteriesAdmit(l, "third", 1, 2));
    CHECK(l.rites.size() == 2);
    MysteriesLoadout none;
    CHECK_FALSE(mysteriesAdmit(none, "a", 1, 0));
    CHECK_FALSE(mysteriesAdmit(none, "a", 1, -4));
    CHECK(none.rites.empty());
}

TEST_CASE("a locked loadout admits nothing new", "[law][mysteries]")
{
    MysteriesLoadout l;
    l.locked = true;
    CHECK_FALSE(mysteriesAdmit(l, "eleusis", 3, 5));
    CHECK(l.rites.empty());
}

TEST_CASE("casting spends a use until the rite is spent", "[law][mysteries]")
{
    MysteriesLoadout l;
    REQUIRE(mysteriesAdmit(l, "eleusis", 2, 3));
    CHECK(mysteriesCast(l, "eleusis"));
    CHECK(l.rites[0].usesLeft == 1);
    CHECK(mysteriesCast(l, "eleusis"));
    CHECK_FALSE(mysteriesCast(l, "eleusis"));
    CHECK(l.rites[0].usesLeft == 0);
    CHECK_FALSE(mysteriesCast(l, "unknown"));
    CHECK_FALSE(mysteriesCast(l, ""));
}

TEST_CASE("a revoked rite cannot be cast and keeps its uses", "[law][mysteries]")
{
    MysteriesLoadout l;
    REQUIRE(mysteriesAdmit(l, "eleusis", 2, 3));
    CHECK(mysteriesRevoke(l, "eleusis"));
    CHECK_FALSE(mysteriesRevoke(l, "eleusis"));
    CHECK_FALSE(mysteriesRevoke(l, "nope"));
    CHECK_FALSE(mysteriesCast(l, "eleusis"));
    CHECK(l.rites[0].usesLeft == 2);
}

TEST_CASE("the oath ceiling grows only up to the cap", "[law][oath]")
{
    CHECK(oathCeiling(100, 1000) == 100);
    CHECK(oathCeiling(100, 2000) == 200);
    CHECK(oathCeiling(100, 50000) == 200);
    CHECK(oathCeiling(-1, 2000) == 0);
    CHECK(oathCeiling(0, 2000) == 0);
    CHECK(oathCeiling(kI64Max, 2000) == std::numeric_limits<std::int32_t>::max());
    CHECK(oathCeiling(1000000, 1500) == 1500000);
}

TEST_CASE("borrowing stops at the ceiling and rejects bad amounts", "[law][oath]")
{
    OathDebt o{0, 100};
    CHECK(oathBorrow(o, 60));
    CHECK(o.debt == 60);
    CHECK_FALSE(oathBorrow(o, 41));
    CHECK(o.debt == 60);
    CHECK(oathBorrow(o, 40));
    CHECK(o.debt == 100);
    CHECK_FALSE(oathBorrow(o, 1));
    CHECK_FALSE(oathBorrow(o, 0));
    CHECK_FALSE(oathBorrow(o, -10));
    CHECK_FALSE(oathBorrow(o, kI64Max));
    CHECK_FALSE(oathBorrow(o, kI64Min));
    CHECK(o.debt == 100);
}

TEST_CASE("borrowing cannot overflow", "[law][oath]")
{
    OathDebt o{kI64Max - 1, kI64Max};
    CHECK_FALSE(oathBorrow(o, 5));
    CHECK(o.debt == kI64Max - 1);
    OathDebt neg{-5, 100};
    CHECK_FALSE(oathBorrow(neg, 1));
}

TEST_CASE("settling never absolves", "[law][oath]")
{
    OathDebt o{1000, 2000};
    CHECK(oathSettle(o, 50) == 50);
    CHECK(o.debt == 950);
    CHECK(oathSettle(o, 100000) == 95);
    CHECK(o.debt == 855);
    CHECK(oathSettle(o, 0) == 0);
    CHECK(oathSettle(o, -4) == 0);
    CHECK(o.debt == 855);
    OathDebt small{9, 100};
    CHECK(oathSettle(small, 100) == 0);
    CHECK(small.debt == 9);
    OathDebt bad{-3, 100};
    CHECK(oathSettle(bad, 5) == 0);
    CHECK(bad.debt == -3);
    OathDebt big{kI64Max, kI64Max};
    CHECK(oathSettle(big, kI64Max) > 0);
    CHECK(big.debt > 0);
}

TEST_CASE("the consequence tier climbs with the meter", "[law][oath]")
{
    CHECK(oathTier({0, 1000}) == OathTier::Clear);
    CHECK(oathTier({249, 1000}) == OathTier::Clear);
    CHECK(oathTier({250, 1000}) == OathTier::Warned);
    CHECK(oathTier({599, 1000}) == OathTier::Warned);
    CHECK(oathTier({600, 1000}) == OathTier::Marked);
    CHECK(oathTier({899, 1000}) == OathTier::Marked);
    CHECK(oathTier({900, 1000}) == OathTier::Called);
    CHECK(oathTier({1000, 1000}) == OathTier::Called);
    CHECK(oathTier({5000, 1000}) == OathTier::Called);
}

TEST_CASE("the tier handles empty ceilings and huge values", "[law][oath]")
{
    CHECK(oathTier({0, 0}) == OathTier::Clear);
    CHECK(oathTier({1, 0}) == OathTier::Called);
    CHECK(oathTier({1, -5}) == OathTier::Called);
    CHECK(oathTier({-1, 1000}) == OathTier::Clear);
    CHECK(oathTier({kI64Max, kI64Max}) == OathTier::Called);
    CHECK(oathTier({kI64Max / 2, kI64Max}) == OathTier::Warned);
    CHECK(oathTier({kI64Max, 1}) == OathTier::Called);
}

TEST_CASE("temenos strength is the floor off the ground and full on it", "[law][temenos]")
{
    CHECK(temenosStrengthPermille(0.0f, 200) == 200);
    CHECK(temenosStrengthPermille(1.0f, 200) == 1000);
    CHECK(temenosStrengthPermille(0.5f, 200) == 600);
    CHECK(temenosStrengthPermille(0.5f, 0) == 500);
    CHECK(temenosStrengthPermille(0.0f, 0) == 0);
    CHECK(temenosStrengthPermille(0.7f, 1000) == 1000);
}

TEST_CASE("temenos strength is monotonic in the field", "[law][temenos]")
{
    std::int64_t prev = -1;
    for (int i = 0; i <= 100; ++i) {
        const std::int64_t s = temenosStrengthPermille(static_cast<float>(i) / 100.0f, 150);
        CHECK(s >= prev);
        prev = s;
    }
    CHECK(prev == 1000);
}

TEST_CASE("temenos strength survives hostile input", "[law][temenos]")
{
    CHECK(temenosStrengthPermille(kNaN, 200) == 200);
    CHECK(temenosStrengthPermille(kInf, 200) == 1000);
    CHECK(temenosStrengthPermille(-kInf, 200) == 200);
    CHECK(temenosStrengthPermille(-3.0f, 200) == 200);
    CHECK(temenosStrengthPermille(7.0f, 200) == 1000);
    CHECK(temenosStrengthPermille(0.5f, -50) == 500);
    CHECK(temenosStrengthPermille(0.0f, 5000) == 1000);
    CHECK(temenosStrengthPermille(0.0f, kI64Min) == 0);
    CHECK(temenosStrengthPermille(0.0f, kI64Max) == 1000);
}

TEST_CASE("temenos scales a value and never exceeds it", "[law][temenos]")
{
    CHECK(temenosScaled(100, 1.0f, 0) == 100);
    CHECK(temenosScaled(100, 0.0f, 0) == 0);
    CHECK(temenosScaled(100, 0.0f, 250) == 25);
    CHECK(temenosScaled(100, 0.5f, 0) == 50);
    CHECK(temenosScaled(-5, 1.0f, 0) == 0);
    CHECK(temenosScaled(0, 1.0f, 0) == 0);
    CHECK(temenosScaled(kI64Max, 1.0f, 0) == kI64Max);
    CHECK(temenosScaled(kI64Max, 0.5f, 0) <= kI64Max / 2 + 1);
    CHECK(temenosScaled(kI64Max, 0.5f, 0) >= kI64Max / 2 - 1);
    CHECK(temenosScaled(kI64Max, kNaN, 0) == 0);
}

TEST_CASE("temenos power follows the merged sanctuary field", "[law][temenos]")
{
    const std::vector<TemenosSanctuary> grove{{0.0f, 0.0f, 100.0f, 1.0f}};
    const std::int64_t atCentre = temenosScaled(100, temenosField(grove, 0.0f, 0.0f), 100);
    const std::int64_t halfway = temenosScaled(100, temenosField(grove, 50.0f, 0.0f), 100);
    const std::int64_t outside = temenosScaled(100, temenosField(grove, 500.0f, 0.0f), 100);
    CHECK(atCentre == 100);
    CHECK(outside == 10);
    CHECK(halfway > outside);
    CHECK(halfway < atCentre);
    CHECK(temenosScaled(100, temenosField(grove, kNaN, 0.0f), 100) == outside);
}
