#include "ichor/progression/failure_states.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace ichor;

namespace {

FailureEvent event(FailureKind kind, std::string subject, std::string cause, std::uint32_t amount, std::uint64_t tick)
{
    return FailureEvent{kind, std::move(subject), std::move(cause), amount, tick};
}

}

TEST_CASE("a recorded failure reads back with every field", "[failure]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 12, 100)) == RecordResult::Ok);
    const auto all = failures(ledger);
    REQUIRE(all.size() == 1);
    CHECK(all[0].kind == FailureKind::Cargo);
    CHECK(all[0].subject == "salt");
    CHECK(all[0].cause == "ambush");
    CHECK(all[0].amount == 12);
    CHECK(all[0].tick == 100);
}

TEST_CASE("failures come back in ledger order and filter by kind and subject", "[failure]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 5, 10)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Standing, "harl", "broken-oath", 30, 20)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "flood", 7, 30)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Route, "pass-9", "collapse", 0, 40)) == RecordResult::Ok);

    CHECK(failures(ledger).size() == 4);
    const auto cargo = failures(ledger, FailureKind::Cargo);
    REQUIRE(cargo.size() == 2);
    CHECK(cargo[0].cause == "ambush");
    CHECK(cargo[1].cause == "flood");
    CHECK(failuresFor(ledger, "harl").size() == 1);
    CHECK(failuresFor(ledger, "nobody").empty());
    CHECK(failuresFor(ledger, "salt", FailureKind::Standing).empty());
}

TEST_CASE("totals add amounts per kind and subject", "[failure]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 5, 10)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "flood", 7, 30)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "grain", "flood", 100, 30)) == RecordResult::Ok);
    REQUIRE(recordFailure(ledger, event(FailureKind::Time, "world-clock", "recovery", 604800, 31)) == RecordResult::Ok);
    CHECK(totalLost(ledger, FailureKind::Cargo, "salt") == 12);
    CHECK(totalLost(ledger, FailureKind::Cargo, "grain") == 100);
    CHECK(totalLost(ledger, FailureKind::Time, "world-clock") == 604800);
    CHECK(totalLost(ledger, FailureKind::Standing, "salt") == 0);
}

TEST_CASE("totals use 64 bits so many large losses cannot overflow", "[failure]")
{
    ConsequenceLedger ledger;
    for (std::uint64_t i = 0; i < 5; ++i) {
        REQUIRE(recordFailure(ledger, event(FailureKind::Time, "world-clock", "recovery", kMaxFailureAmount, i)) == RecordResult::Ok);
    }
    CHECK(totalLost(ledger, FailureKind::Time, "world-clock") == std::uint64_t{5} * kMaxFailureAmount);
}

TEST_CASE("companion and route losses carry no amount", "[failure]")
{
    ConsequenceLedger ledger;
    CHECK(recordFailure(ledger, event(FailureKind::Companion, "ilse", "captured", 1, 5)) == RecordResult::InvalidAmount);
    CHECK(recordFailure(ledger, event(FailureKind::Route, "pass-9", "collapse", 1, 5)) == RecordResult::InvalidAmount);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 0, 5)) == RecordResult::InvalidAmount);
    CHECK(recordFailure(ledger, event(FailureKind::Time, "world-clock", "recovery", kMaxFailureAmount + 1, 5)) == RecordResult::InvalidAmount);
    CHECK(recordFailure(ledger, event(FailureKind::Time, "world-clock", "recovery", std::numeric_limits<std::uint32_t>::max(), 5))
        == RecordResult::InvalidAmount);
    CHECK(ledger.empty());
    CHECK(recordFailure(ledger, event(FailureKind::Companion, "ilse", "captured", 0, 5)) == RecordResult::Ok);
}

TEST_CASE("bad keys are rejected and leave the ledger unchanged", "[failure]")
{
    ConsequenceLedger ledger;
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "", "ambush", 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "Salt", "ambush", 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", "", 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", "a;b", 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, std::string(kMaxFailureKeyBytes + 1, 'a'), "x", 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", std::string(100000, 'a'), 1, 1)) == RecordResult::InvalidKey);
    CHECK(recordFailure(ledger, event(static_cast<FailureKind>(99), "salt", "x", 1, 1)) == RecordResult::InvalidKey);
    CHECK(ledger.empty());
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, std::string(kMaxFailureKeyBytes, 'a'), std::string(kMaxFailureKeyBytes, 'b'), 1,
              std::numeric_limits<std::uint64_t>::max()))
        == RecordResult::Ok);
}

TEST_CASE("the same failure twice is a duplicate, a different amount is not", "[failure]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 5, 10)) == RecordResult::Ok);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 5, 10)) == RecordResult::DuplicateId);
    CHECK(recordFailure(ledger, event(FailureKind::Cargo, "salt", "ambush", 6, 10)) == RecordResult::Ok);
    CHECK(ledger.size() == 2);
}

TEST_CASE("failures survive a save and load, and malformed or foreign facts are skipped", "[failure]")
{
    ConsequenceLedger ledger;
    REQUIRE(recordFailure(ledger, event(FailureKind::Standing, "harl", "broken-oath", 30, 20)) == RecordResult::Ok);
    REQUIRE(ledger.append({"x1", "harl", "failure-standing", "no-semicolon", 1}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x2", "harl", "failure-standing", "oath;007", 2}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x3", "harl", "failure-standing", "oath;99999999999999999999", 3}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x4", "harl", "failure-standing", "oath;-5", 4}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x5", "harl", "burned", "oath;5", 5}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x6", "harl", "failure-standing", "Oath;5;", 6}) == AppendResult::Ok);
    REQUIRE(ledger.append({"x7", "harl", "failure-companion", "oath;3", 7}) == AppendResult::Ok);
    const auto loaded = ConsequenceLedger::parse(ledger.serialize());
    REQUIRE(loaded);
    const auto read = failures(*loaded);
    REQUIRE(read.size() == 1);
    CHECK(read[0].amount == 30);
    CHECK(totalLost(*loaded, FailureKind::Standing, "harl") == 30);
}

TEST_CASE("recording is a pure function of the ledger and the event", "[failure]")
{
    ConsequenceLedger a;
    ConsequenceLedger b;
    for (ConsequenceLedger* ledger : {&a, &b}) {
        REQUIRE(recordFailure(*ledger, event(FailureKind::Route, "pass-9", "collapse", 0, 40)) == RecordResult::Ok);
        REQUIRE(recordFailure(*ledger, event(FailureKind::Time, "world-clock", "recovery", 600, 41)) == RecordResult::Ok);
    }
    CHECK(a.digest() == b.digest());
}
