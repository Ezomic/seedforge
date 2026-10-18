#include "ichor/progression/discovery_xp.h"
#include "ichor/progression/stat_growth.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::vector<SpineStat> campaign()
{
    return {
        {"attunement", 10, 17, 250},
        {"carry", 40, 70, 200},
        {"health", 100, 180, 300},
        {"stamina", 100, 170, 250},
    };
}

}

TEST_CASE("stat progress rises with diminishing returns", "[progression][stats]")
{
    CHECK(statProgressPermille(0) == 0);
    CHECK(statProgressPermille(kStatHalfXp) == 500);
    const std::int64_t early = statProgressPermille(1000) - statProgressPermille(0);
    const std::int64_t late = statProgressPermille(101000) - statProgressPermille(100000);
    CHECK(early > 10 * late);
    CHECK(statProgressPermille(100000) < 1000);
}

TEST_CASE("stat progress is safe for extreme xp", "[progression][stats]")
{
    CHECK(statProgressPermille(-1) == 0);
    CHECK(statProgressPermille(std::numeric_limits<std::int64_t>::min()) == 0);
    CHECK(statProgressPermille(std::numeric_limits<std::int64_t>::max()) == statProgressPermille(kStatMaxXp));
    CHECK(statProgressPermille(kStatMaxXp) < 1000);
}

TEST_CASE("stat values start at start and never pass end", "[progression][stats]")
{
    for (const SpineStat& stat : campaign()) {
        CHECK(statValueAtXp(stat, 0) == stat.start);
        CHECK(statValueAtXp(stat, 20000) > stat.start);
        CHECK(statValueAtXp(stat, 20000) < stat.end);
        CHECK(statValueAtXp(stat, std::numeric_limits<std::int64_t>::max()) <= stat.end);
    }
}

TEST_CASE("stat values reject hostile stat records", "[progression][stats]")
{
    CHECK(statValueAtXp({"x", 100, 50, 0}, 5000) == 100);
    CHECK(statValueAtXp({"x", 100, std::numeric_limits<std::int64_t>::max(), 0}, 5000) == 100);
    CHECK(statValueAtXp({"x", std::numeric_limits<std::int64_t>::min(), 5, 0}, 5000) == std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("total power at any xp stays under the 2x cap", "[progression][stats]")
{
    const auto table = campaign();
    const std::int64_t full = totalGrowthPermille(table);
    REQUIRE(full > 1500);
    REQUIRE(full <= kPowerCapPermille);
    std::int64_t previous = totalGrowthPermille(statsAtXp(table, 0));
    CHECK(previous == 1000);
    for (const std::int64_t xp : std::vector<std::int64_t>{100, 1000, 5000, 20000, 1000000, kStatMaxXp, std::numeric_limits<std::int64_t>::max()}) {
        const std::int64_t now = totalGrowthPermille(statsAtXp(table, xp));
        CHECK(now >= previous);
        CHECK(now <= full);
        CHECK(now <= kPowerCapPermille);
        previous = now;
    }
}

TEST_CASE("an over-cap table is reported, not hidden by the curve", "[progression][stats]")
{
    std::vector<SpineStat> greedy = {
        {"attunement", 10, 40, 250}, {"carry", 40, 160, 200}, {"health", 100, 400, 300}, {"stamina", 100, 400, 250},
    };
    CHECK(totalGrowthPermille(greedy) > kPowerCapPermille);
    std::vector<std::string> problems;
    checkPowerGrowthCap("greedy", greedy, problems);
    CHECK_FALSE(problems.empty());
}

TEST_CASE("discovery xp feeds stats once per discovery id", "[progression][stats][discovery]")
{
    const auto table = campaign();
    DiscoveryLedger ledger;
    for (int i = 0; i < 3; ++i) {
        ledger.record({"cave_a", DiscoveryKind::Poi, 1.0});
    }
    CHECK(ledger.count() == 1);
    const std::int64_t once = totalGrowthPermille(statsAtXp(table, ledger.totalXp()));

    DiscoveryLedger many;
    for (int i = 0; i < 200; ++i) {
        many.record({"site_" + std::to_string(i), DiscoveryKind::Poi, 1.0});
    }
    const std::int64_t lots = totalGrowthPermille(statsAtXp(table, many.totalXp()));
    CHECK(once < lots);
    CHECK(lots <= kPowerCapPermille);
    CHECK(statValueAtXp(table[2], ledger.totalXp()) < statValueAtXp(table[2], many.totalXp()));
}
