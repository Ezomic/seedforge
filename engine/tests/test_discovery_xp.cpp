#include "ichor/progression/discovery_xp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

using namespace ichor;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

Discovery make(const std::string& id, DiscoveryKind kind, double score)
{
    return Discovery{id, kind, score};
}

}

TEST_CASE("discovery xp scales with score and kind", "[progression][discovery]")
{
    CHECK(discoveryXp(DiscoveryKind::Poi, 1.0, 0) == 100);
    CHECK(discoveryXp(DiscoveryKind::Poi, 0.5, 0) == 50);
    CHECK(discoveryXp(DiscoveryKind::Landmark, 1.0, 0) == 40);
    CHECK(discoveryXp(DiscoveryKind::Vista, 1.0, 0) == 20);
    CHECK(discoveryXp(DiscoveryKind::Poi, 0.0, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, 0.001, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, 0.999, 0) == 99);
    CHECK(discoveryXp(DiscoveryKind::Poi, 0.9, 0) > discoveryXp(DiscoveryKind::Poi, 0.1, 0));
}

TEST_CASE("discovery xp rejects bad scores", "[progression][discovery]")
{
    CHECK(discoveryXp(DiscoveryKind::Poi, kNaN, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, kInf, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, -kInf, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, -0.0001, 0) == 0);
    CHECK(discoveryXp(DiscoveryKind::Poi, -1e300, 0) == 0);
    CHECK(discoveryXp(static_cast<DiscoveryKind>(200), 1.0, 0) == 0);
}

TEST_CASE("discovery xp saturates extreme scores", "[progression][discovery]")
{
    CHECK(discoveryXp(DiscoveryKind::Poi, 1.0, 0) == discoveryXp(DiscoveryKind::Poi, 1.5, 0));
    CHECK(discoveryXp(DiscoveryKind::Poi, 1e300, 0) == 100);
    CHECK(discoveryXp(DiscoveryKind::Poi, std::numeric_limits<double>::max(), 0) == 100);
    CHECK(discoveryXp(DiscoveryKind::Poi, std::numeric_limits<double>::denorm_min(), 0) == 0);
}

TEST_CASE("repeats of a kind diminish to a floor", "[progression][discovery]")
{
    CHECK(discoveryRepeatPermille(0) == 1000);
    CHECK(discoveryRepeatPermille(1) == 970);
    std::int64_t previous = 1000;
    for (std::size_t n = 1; n < 200; ++n) {
        const auto m = discoveryRepeatPermille(n);
        CHECK(m <= previous);
        CHECK(m >= kRepeatFloorPermille);
        previous = m;
    }
    CHECK(discoveryRepeatPermille(200) == kRepeatFloorPermille);
    CHECK(discoveryRepeatPermille(std::numeric_limits<std::size_t>::max()) == kRepeatFloorPermille);
    CHECK(discoveryXp(DiscoveryKind::Poi, 1.0, 1) < discoveryXp(DiscoveryKind::Poi, 1.0, 0));
    CHECK(discoveryXp(DiscoveryKind::Poi, 1.0, std::numeric_limits<std::size_t>::max()) == 10);
}

TEST_CASE("a discovery counts once per stable id", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    const auto first = ledger.record(make("poi.ruin.7", DiscoveryKind::Poi, 1.0));
    CHECK(first.status == DiscoveryStatus::Awarded);
    CHECK(first.xp == 100);
    const auto again = ledger.record(make("poi.ruin.7", DiscoveryKind::Poi, 1.0));
    CHECK(again.status == DiscoveryStatus::Duplicate);
    CHECK(again.xp == 0);
    CHECK(ledger.totalXp() == 100);
    CHECK(ledger.count() == 1);
    CHECK(ledger.has("poi.ruin.7"));
    CHECK_FALSE(ledger.has("poi.ruin.8"));
}

TEST_CASE("a duplicate id cannot change kind or score to earn again", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    ledger.record(make("x", DiscoveryKind::Vista, 0.1));
    const auto dup = ledger.record(make("x", DiscoveryKind::Poi, 1.0));
    CHECK(dup.status == DiscoveryStatus::Duplicate);
    CHECK(ledger.totalXp() == 2);
    CHECK(ledger.countOf(DiscoveryKind::Poi) == 0);
    CHECK(ledger.countOf(DiscoveryKind::Vista) == 1);
}

TEST_CASE("repeats within a kind pay less, other kinds are independent", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    const auto a = ledger.record(make("a", DiscoveryKind::Poi, 1.0));
    const auto b = ledger.record(make("b", DiscoveryKind::Poi, 1.0));
    const auto c = ledger.record(make("c", DiscoveryKind::Landmark, 1.0));
    CHECK(a.xp == 100);
    CHECK(b.xp == 97);
    CHECK(c.xp == 40);
    CHECK(ledger.totalXp() == 237);
    CHECK(ledger.countOf(DiscoveryKind::Poi) == 2);
}

TEST_CASE("invalid discoveries are rejected without side effects", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    CHECK(ledger.record(make("", DiscoveryKind::Poi, 1.0)).status == DiscoveryStatus::InvalidId);
    CHECK(ledger.record(make(std::string(kMaxDiscoveryIdBytes + 1, 'a'), DiscoveryKind::Poi, 1.0)).status == DiscoveryStatus::InvalidId);
    CHECK(ledger.record(make("ok", static_cast<DiscoveryKind>(9), 1.0)).status == DiscoveryStatus::InvalidKind);
    CHECK(ledger.record(make("nan", DiscoveryKind::Poi, kNaN)).status == DiscoveryStatus::InvalidScore);
    CHECK(ledger.record(make("neg", DiscoveryKind::Poi, -0.5)).status == DiscoveryStatus::InvalidScore);
    CHECK(ledger.record(make("inf", DiscoveryKind::Poi, kInf)).status == DiscoveryStatus::InvalidScore);
    CHECK(ledger.count() == 0);
    CHECK(ledger.totalXp() == 0);

    const auto fixed = ledger.record(make("nan", DiscoveryKind::Poi, 1.0));
    CHECK(fixed.status == DiscoveryStatus::Awarded);
    CHECK(fixed.xp == 100);
}

TEST_CASE("id length boundary and zero score", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    CHECK(ledger.record(make(std::string(kMaxDiscoveryIdBytes, 'a'), DiscoveryKind::Poi, 1.0)).status == DiscoveryStatus::Awarded);
    const auto zero = ledger.record(make("zero", DiscoveryKind::Poi, 0.0));
    CHECK(zero.status == DiscoveryStatus::Awarded);
    CHECK(zero.xp == 0);
    CHECK(ledger.has("zero"));
    CHECK(ledger.record(make("zero", DiscoveryKind::Poi, 1.0)).status == DiscoveryStatus::Duplicate);
}

TEST_CASE("the ledger is bounded and the total never exceeds its cap", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    for (std::size_t i = 0; i < kMaxDiscoveries; ++i) {
        const auto r = ledger.record(make("d" + std::to_string(i), DiscoveryKind::Vista, 0.0));
        REQUIRE(r.status == DiscoveryStatus::Awarded);
    }
    CHECK(ledger.record(make("overflow", DiscoveryKind::Poi, 1.0)).status == DiscoveryStatus::LedgerFull);
    CHECK(ledger.count() == kMaxDiscoveries);
    CHECK(ledger.totalXp() >= 0);
    CHECK(ledger.totalXp() <= kMaxTotalXp);
}

TEST_CASE("a long run of discoveries stays bounded and non-decreasing", "[progression][discovery]")
{
    DiscoveryLedger ledger;
    std::int64_t previous = 0;
    for (std::size_t i = 0; i < 5000; ++i) {
        const auto r = ledger.record(make("p" + std::to_string(i), DiscoveryKind::Poi, 1.0));
        CHECK(r.xp >= 10);
        CHECK(r.xp <= 100);
        CHECK(ledger.totalXp() >= previous);
        previous = ledger.totalXp();
    }
    CHECK(ledger.totalXp() <= kMaxTotalXp);
    CHECK(ledger.totalXp() > 100);
}
