#include "ichor/progression/downed_revive.h"
#include "ichor/progression/failure_states.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <tuple>
#include <vector>

using namespace ichor;

namespace {

DownedConfig config()
{
    DownedConfig c;
    c.bleedOutSeconds = 10.0;
    c.reviveSeconds = 4.0;
    c.cost = ReviveCost{2, "salt", 6, 120};
    return c;
}

DownedMachine downedMachine()
{
    DownedMachine m(config());
    REQUIRE(m.down() == DownedResult::Ok);
    return m;
}

}

TEST_CASE("a new machine is alive and ticking does nothing", "[downed]")
{
    DownedMachine m(config());
    CHECK(m.status() == DownedStatus::Alive);
    CHECK(m.tick(100.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Alive);
}

TEST_CASE("down starts the bleed-out timer and cannot repeat", "[downed]")
{
    DownedMachine m(config());
    CHECK(m.down() == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Downed);
    CHECK(m.bleedRemaining() == 10.0);
    CHECK(m.down() == DownedResult::InvalidState);
}

TEST_CASE("bleeding out ends in death and stays dead", "[downed]")
{
    auto m = downedMachine();
    CHECK(m.tick(9.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Downed);
    CHECK(m.bleedRemaining() == 1.0);
    CHECK(m.tick(1.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Dead);
    ReviveResources res{5};
    ConsequenceLedger ledger;
    CHECK(m.beginRevive(res, ledger, 1) == DownedResult::InvalidState);
    CHECK(m.down() == DownedResult::InvalidState);
    CHECK(m.tick(5.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Dead);
}

TEST_CASE("a revive spends charges and writes cargo and time to the ledger", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{5};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 700) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Reviving);
    CHECK(res.charges == 3);
    CHECK(totalLost(ledger, FailureKind::Cargo, "salt") == 6);
    CHECK(totalLost(ledger, FailureKind::Time, "world-clock") == 120);
    REQUIRE(failures(ledger).size() == 2);
    CHECK(failures(ledger)[0].tick == 700);
}

TEST_CASE("a revive completes after its duration", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{2};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.tick(3.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Reviving);
    CHECK(m.tick(1.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Alive);
    CHECK(res.charges == 0);
}

TEST_CASE("a revive without enough charges costs nothing and changes nothing", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{1};
    ConsequenceLedger ledger;
    CHECK(m.beginRevive(res, ledger, 1) == DownedResult::NotEnoughCharges);
    CHECK(m.status() == DownedStatus::Downed);
    CHECK(res.charges == 1);
    CHECK(ledger.empty());
}

TEST_CASE("reviving an alive player is rejected", "[downed]")
{
    DownedMachine m(config());
    ReviveResources res{5};
    ConsequenceLedger ledger;
    CHECK(m.beginRevive(res, ledger, 1) == DownedResult::InvalidState);
    CHECK(res.charges == 5);
    CHECK(ledger.empty());
}

TEST_CASE("a double revive is rejected and charged once", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{10};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.beginRevive(res, ledger, 1) == DownedResult::InvalidState);
    CHECK(m.beginRevive(res, ledger, 2) == DownedResult::InvalidState);
    CHECK(res.charges == 8);
    CHECK(ledger.size() == 2);
}

TEST_CASE("a revive that loses the race to the timer ends in death with the cost spent", "[downed]")
{
    auto m = downedMachine();
    CHECK(m.tick(8.0) == DownedResult::Ok);
    ReviveResources res{2};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.tick(2.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Dead);
    CHECK(res.charges == 0);
    CHECK(ledger.size() == 2);
}

TEST_CASE("a revive that finishes first beats the timer", "[downed]")
{
    auto m = downedMachine();
    CHECK(m.tick(5.0) == DownedResult::Ok);
    ReviveResources res{2};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.tick(4.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Alive);
}

TEST_CASE("a revive and the timer finishing in the same tick is a revive", "[downed]")
{
    auto m = downedMachine();
    CHECK(m.tick(6.0) == DownedResult::Ok);
    ReviveResources res{2};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.tick(4.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Alive);
}

TEST_CASE("the race resolves to death whatever the step size", "[downed]")
{
    for (double step : {0.25, 1.0, 100.0}) {
        auto m = downedMachine();
        CHECK(m.tick(7.0) == DownedResult::Ok);
        ReviveResources res{2};
        ConsequenceLedger ledger;
        REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
        for (int i = 0; i < 100 && m.status() == DownedStatus::Reviving; ++i) REQUIRE(m.tick(step) == DownedResult::Ok);
        CHECK(m.status() == DownedStatus::Dead);
    }
}

TEST_CASE("cancelling a revive returns to downed, keeps the cost spent and the timer running", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{4};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 1) == DownedResult::Ok);
    CHECK(m.tick(2.0) == DownedResult::Ok);
    CHECK(m.cancelRevive() == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Downed);
    CHECK(m.bleedRemaining() == 8.0);
    CHECK(res.charges == 2);
    CHECK(m.cancelRevive() == DownedResult::InvalidState);
    CHECK(m.tick(8.0) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Dead);
}

TEST_CASE("a repeat revive at the same world tick is rejected by the ledger and costs nothing", "[downed]")
{
    auto m = downedMachine();
    ReviveResources res{10};
    ConsequenceLedger ledger;
    REQUIRE(m.beginRevive(res, ledger, 5) == DownedResult::Ok);
    REQUIRE(m.cancelRevive() == DownedResult::Ok);
    CHECK(m.beginRevive(res, ledger, 5) == DownedResult::LedgerRejected);
    CHECK(res.charges == 8);
    CHECK(ledger.size() == 2);
    CHECK(m.status() == DownedStatus::Downed);
    CHECK(m.beginRevive(res, ledger, 6) == DownedResult::Ok);
    CHECK(ledger.size() == 4);
}

TEST_CASE("hostile tick values are rejected and change nothing", "[downed]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double bad : {nan, inf, -inf, -1.0, -0.001, kMaxDownedSeconds * 2.0, std::numeric_limits<double>::max()}) {
        auto m = downedMachine();
        CHECK(m.tick(bad) == DownedResult::InvalidTick);
        CHECK(m.status() == DownedStatus::Downed);
        CHECK(m.bleedRemaining() == 10.0);
    }
}

TEST_CASE("a zero tick changes nothing and the largest allowed tick kills", "[downed]")
{
    auto m = downedMachine();
    CHECK(m.tick(0.0) == DownedResult::Ok);
    CHECK(m.bleedRemaining() == 10.0);
    CHECK(m.tick(kMaxDownedSeconds) == DownedResult::Ok);
    CHECK(m.status() == DownedStatus::Dead);
}

TEST_CASE("invalid configs make every command fail", "[downed]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<DownedConfig> bad(8, config());
    bad[0].bleedOutSeconds = 0.0;
    bad[1].bleedOutSeconds = nan;
    bad[2].reviveSeconds = -1.0;
    bad[3].reviveSeconds = kMaxDownedSeconds + 1.0;
    bad[4].cost.charges = 0;
    bad[5].cost.cargoSubject = "Bad Subject!";
    bad[6].cost.cargoAmount = 0;
    bad[7].cost.timeTicks = kMaxFailureAmount + 1;
    for (const auto& c : bad) {
        CHECK_FALSE(validConfig(c));
        DownedMachine m(c);
        ReviveResources res{9};
        ConsequenceLedger ledger;
        CHECK(m.down() == DownedResult::InvalidConfig);
        CHECK(m.beginRevive(res, ledger, 1) == DownedResult::InvalidConfig);
        CHECK(m.tick(1.0) == DownedResult::InvalidConfig);
        CHECK(m.cancelRevive() == DownedResult::InvalidConfig);
        CHECK(m.status() == DownedStatus::Alive);
        CHECK(res.charges == 9);
        CHECK(ledger.empty());
    }
    CHECK(validConfig(config()));
}

TEST_CASE("a ledger that rejects the second fact is left untouched", "[downed]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, FailureEvent{FailureKind::Time, "world-clock", "revive", 120, 9}) == RecordResult::Ok);
    auto m = downedMachine();
    ReviveResources res{5};
    CHECK(m.beginRevive(res, ledger, 9) == DownedResult::LedgerRejected);
    CHECK(res.charges == 5);
    CHECK(ledger.size() == 1);
    CHECK(m.status() == DownedStatus::Downed);
}

TEST_CASE("the same commands replay to the same state, ledger and resources", "[downed]")
{
    auto run = [] {
        auto m = downedMachine();
        ReviveResources res{3};
        ConsequenceLedger ledger;
        (void)m.tick(2.5);
        (void)m.beginRevive(res, ledger, 40);
        (void)m.tick(1.5);
        (void)m.cancelRevive();
        (void)m.tick(0.5);
        (void)m.beginRevive(res, ledger, 41);
        (void)m.tick(3.0);
        return std::make_tuple(m.status(), m.bleedRemaining(), m.reviveRemaining(), res.charges, ledger.digest());
    };
    CHECK(run() == run());
    CHECK(std::get<0>(run()) == DownedStatus::Downed);
    CHECK(std::get<3>(run()) == 1);
}
