#include "test_temp.h"
#include "ichor/data/data_set.h"
#include "ichor/economy/access_fees.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

const std::vector<Faction> kFactions = {
    {"archonate", "The Archonate", FactionRole::Incumbent},
    {"archaists", "The Archaists", FactionRole::Challenger},
    {"mnemones", "The Mnemones", FactionRole::Custodian},
};

const std::string kTiersJson = R"([
    {"id": "hostile", "name": "Hostile", "threshold": -1000},
    {"id": "wary", "name": "Wary", "threshold": -250},
    {"id": "neutral", "name": "Neutral", "threshold": 0},
    {"id": "friendly", "name": "Friendly", "threshold": 250},
    {"id": "allied", "name": "Allied", "threshold": 750}
])";

const std::string kGatesJson = R"([
    {"id": "safe_passage", "faction": "archonate", "min_tier": "friendly"},
    {"id": "trainer_basic", "faction": "archaists", "min_tier": "friendly"},
    {"id": "base_access", "faction": "mnemones", "min_tier": "allied"}
])";

std::vector<StandingTier> tiers()
{
    std::vector<StandingTier> out;
    std::vector<std::string> problems;
    REQUIRE(parseStandingTiers("t.json", kTiersJson, out, problems));
    return out;
}

std::vector<StandingGate> gates()
{
    std::vector<StandingGate> out;
    std::vector<std::string> problems;
    REQUIRE(parseStandingGates("g.json", kGatesJson, tiers(), kFactions, out, problems));
    return out;
}

FactionStanding standing(std::int32_t archonate, std::int32_t archaists = 0, std::int32_t mnemones = 0)
{
    FactionStanding s(kFactions, tiers(), gates());
    REQUIRE(s.change("archonate", archonate));
    REQUIRE(s.change("archaists", archaists));
    REQUIRE(s.change("mnemones", mnemones));
    return s;
}

FactionFee archonateFee()
{
    FactionFee fee;
    fee.faction = "archonate";
    fee.duesAmount = 100;
    fee.duesPeriod = 1000;
    fee.duesMinTier = "wary";
    fee.bribeBase = 50;
    fee.bribePerTier = 25;
    fee.bribeDuration = 200;
    fee.bribeMaxTiers = 2;
    return fee;
}

AccessFees make(std::vector<FactionFee> fees = {archonateFee()})
{
    std::string error;
    auto result = AccessFees::create(std::move(fees), tiers(), gates(), kFactions, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

std::string createError(std::vector<FactionFee> fees)
{
    std::string error;
    CHECK_FALSE(AccessFees::create(std::move(fees), tiers(), gates(), kFactions, error).has_value());
    return error;
}

CurrencyTable shipped()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

Wallet rich(const CurrencyTable& table, std::uint64_t coin)
{
    Wallet wallet;
    REQUIRE(wallet.earn(table, CurrencySlot::Coin, coin, "loot").status == TxStatus::Ok);
    return wallet;
}

}

TEST_CASE("the shipped currency table lets coin pay dues and bribes")
{
    const CurrencyTable table = shipped();
    CHECK(table.canSpendOn(CurrencySlot::Coin, "dues"));
    CHECK(table.canSpendOn(CurrencySlot::Coin, "bribe"));
    CHECK_FALSE(table.canSpendOn(CurrencySlot::Hard, "dues"));
}

TEST_CASE("dues rent access to a gate the standing does not open")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100); // neutral, gate needs friendly
    AccessFees fees = make();

    CHECK_FALSE(fees.hasAccess(s, "safe_passage", 10));
    CHECK(fees.payDues(wallet, table, s, "archonate", 10) == FeeStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Coin) == 400);
    CHECK(fees.duesUntil("archonate") == 1010);
    CHECK(fees.hasAccess(s, "safe_passage", 10));
    CHECK(fees.hasAccess(s, "safe_passage", 1009));
    CHECK_FALSE(fees.hasAccess(s, "safe_passage", 1010));
}

TEST_CASE("a lease opens only its own faction's gates")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100, 100);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    CHECK_FALSE(fees.hasAccess(s, "trainer_basic", 5));
    CHECK_FALSE(fees.hasAccess(s, "no_such_gate", 5));
}

TEST_CASE("a lease lapses when standing falls below the dues tier")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    FactionStanding s = standing(100);
    AccessFees fees = make();
    REQUIRE(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    REQUIRE(s.change("archonate", -500)); // -400 is hostile, below the wary dues tier
    CHECK_FALSE(fees.hasAccess(s, "safe_passage", 5));
}

TEST_CASE("dues are refused below the min tier and cost nothing")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(-600);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::StandingTooLow);
    CHECK(wallet.balance(CurrencySlot::Coin) == 500);
    CHECK_FALSE(fees.duesUntil("archonate"));
}

TEST_CASE("renewing early extends the lease up to the prepaid limit")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 1000);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    CHECK(fees.duesUntil("archonate") == 3000);
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::AlreadyPaid);
    CHECK(wallet.balance(CurrencySlot::Coin) == 700);
    CHECK(fees.payDues(wallet, table, s, "archonate", 1000) == FeeStatus::Ok);
    CHECK(fees.duesUntil("archonate") == 4000);
}

TEST_CASE("a lapsed lease restarts from now, not from the old end")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 1000);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    REQUIRE(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::Ok);
    REQUIRE(fees.payDues(wallet, table, s, "archonate", 5000) == FeeStatus::Ok);
    CHECK(fees.duesUntil("archonate") == 6000);
}

TEST_CASE("dues that cannot be paid change nothing")
{
    const CurrencyTable table = shipped();
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    Wallet poor = rich(table, 99);
    CHECK(fees.payDues(poor, table, s, "archonate", 0) == FeeStatus::Insufficient);
    CHECK(poor.balance(CurrencySlot::Coin) == 99);
    CHECK_FALSE(fees.duesUntil("archonate"));
    CHECK_FALSE(fees.hasAccess(s, "safe_passage", 0));
}

TEST_CASE("dues for unknown factions and factions without dues are refused")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, table, s, "nobody", 0) == FeeStatus::UnknownFaction);
    CHECK(fees.payDues(wallet, table, s, "archaists", 0) == FeeStatus::NotOffered);
    CHECK(wallet.balance(CurrencySlot::Coin) == 500);

    FactionFee noDues = archonateFee();
    noDues.duesAmount = 0;
    noDues.duesPeriod = 0;
    noDues.duesMinTier.clear();
    AccessFees bribesOnly = make({noDues});
    CHECK(bribesOnly.payDues(wallet, table, s, "archonate", 0) == FeeStatus::NotOffered);
}

TEST_CASE("a time near the u64 limit is refused instead of wrapping")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, table, s, "archonate", kMax) == FeeStatus::TimeOverflow);
    CHECK(fees.payDues(wallet, table, s, "archonate", kMax - 3000) == FeeStatus::TimeOverflow);
    CHECK(wallet.balance(CurrencySlot::Coin) == 500);

    CHECK(fees.payBribe(wallet, table, s, "safe_passage", kMax) == FeeStatus::TimeOverflow);
    CHECK(fees.payBribe(wallet, table, s, "safe_passage", kMax - 10) == FeeStatus::TimeOverflow);
    CHECK(wallet.balance(CurrencySlot::Coin) == 500);
}

TEST_CASE("a bribe price grows with each tier the standing is short")
{
    const FactionStanding oneShort = standing(100);  // neutral, friendly needed
    const FactionStanding twoShort = standing(-100); // wary
    const FactionStanding threeShort = standing(-600); // hostile
    const AccessFees fees = make();

    CHECK(fees.quoteBribe(oneShort, "safe_passage", 0).price == 75);
    CHECK(fees.quoteBribe(twoShort, "safe_passage", 0).price == 100);
    CHECK(fees.quoteBribe(threeShort, "safe_passage", 0).status == FeeStatus::StandingTooLow);
    CHECK(fees.quoteBribe(oneShort, "nope", 0).status == FeeStatus::UnknownGate);
    CHECK(fees.quoteBribe(oneShort, "trainer_basic", 0).status == FeeStatus::NotOffered);
}

TEST_CASE("a bribe opens one gate for its duration and then expires")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100);
    AccessFees fees = make();

    CHECK(fees.payBribe(wallet, table, s, "safe_passage", 40) == FeeStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Coin) == 425);
    CHECK(fees.bribeUntil("safe_passage") == 240);
    CHECK(fees.hasAccess(s, "safe_passage", 239));
    CHECK_FALSE(fees.hasAccess(s, "safe_passage", 240));
    CHECK_FALSE(fees.hasAccess(s, "base_access", 100));
}

TEST_CASE("a bribe is not sold for a gate that is already open")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    AccessFees fees = make();

    const FactionStanding friendly = standing(300);
    CHECK(fees.payBribe(wallet, table, friendly, "safe_passage", 0) == FeeStatus::NotNeeded);

    const FactionStanding neutral = standing(100);
    REQUIRE(fees.payBribe(wallet, table, neutral, "safe_passage", 0) == FeeStatus::Ok);
    CHECK(fees.payBribe(wallet, table, neutral, "safe_passage", 10) == FeeStatus::NotNeeded);
    CHECK(wallet.balance(CurrencySlot::Coin) == 425);
}

TEST_CASE("a bribe the wallet cannot cover changes nothing")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 74);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    CHECK(fees.payBribe(wallet, table, s, "safe_passage", 0) == FeeStatus::Insufficient);
    CHECK(wallet.balance(CurrencySlot::Coin) == 74);
    CHECK_FALSE(fees.bribeUntil("safe_passage"));
}

TEST_CASE("a currency table that forbids the sink refuses the payment")
{
    const auto root = ichor::test::outputPath(ICHOR_TEST_OUTPUT, "access-fees-nosink");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "currency");
    std::ofstream(root / "currency" / "currencies.json", std::ios::binary)
        << R"([{"id": "coin", "name": "Coin", "symbol": "c", "earn": ["loot"], "spend": ["trade"], "cap": 1000},
               {"id": "hard", "name": "Charge", "symbol": "ch", "earn": [], "spend": ["power"], "cap": 50}])";
    DataSet data;
    REQUIRE(data.loadDirectory(root));
    std::string error;
    auto table = CurrencyTable::fromTable(*data.table("currency"), error);
    REQUIRE(table.has_value());
    Wallet wallet;
    REQUIRE(wallet.earn(*table, CurrencySlot::Coin, 500, "loot").status == TxStatus::Ok);
    const FactionStanding s = standing(100);
    AccessFees fees = make();
    CHECK(fees.payDues(wallet, *table, s, "archonate", 0) == FeeStatus::NotAllowed);
    CHECK(fees.payBribe(wallet, *table, s, "safe_passage", 0) == FeeStatus::NotAllowed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 500);
    CHECK_FALSE(fees.duesUntil("archonate"));
    CHECK_FALSE(fees.bribeUntil("safe_passage"));
}

TEST_CASE("fee tables with bad terms are refused")
{
    FactionFee fee = archonateFee();
    SECTION("duplicate gate") { std::string e; auto g = gates(); g.push_back(g.front()); CHECK_FALSE(AccessFees::create({fee}, tiers(), g, kFactions, e)); }
    SECTION("unknown faction") { fee.faction = "nobody"; CHECK(createError({fee}).find("unknown faction") != std::string::npos); }
    SECTION("duplicate") { CHECK(createError({fee, fee}).find("twice") != std::string::npos); }
    SECTION("dues without period") { fee.duesPeriod = 0; createError({fee}); }
    SECTION("dues period too long") { fee.duesPeriod = kMaxFeePeriod + 1; createError({fee}); }
    SECTION("dues unknown tier") { fee.duesMinTier = "revered"; createError({fee}); }
    SECTION("period without amount") { fee.duesAmount = 0; createError({fee}); }
    SECTION("huge dues") { fee.duesAmount = kMax; createError({fee}); }
    SECTION("huge bribe step") { fee.bribePerTier = kMax; createError({fee}); }
    SECTION("bribe without base") { fee.bribeBase = 0; createError({fee}); }
    SECTION("bribe without tiers") { fee.bribeMaxTiers = 0; createError({fee}); }
    SECTION("too many bribe tiers") { fee.bribeMaxTiers = kMaxBribeTiers + 1; createError({fee}); }
    SECTION("bribe duration too long") { fee.bribeDuration = kMaxFeePeriod + 1; createError({fee}); }
    SECTION("bribe terms without duration") { fee.bribeDuration = 0; createError({fee}); }
}

TEST_CASE("an empty fee table is valid and charges nothing")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 500);
    const FactionStanding s = standing(100);
    AccessFees fees = make({});
    CHECK(fees.payDues(wallet, table, s, "archonate", 0) == FeeStatus::NotOffered);
    CHECK(fees.payBribe(wallet, table, s, "safe_passage", 0) == FeeStatus::NotOffered);
    CHECK(fees.hasAccess(s, "safe_passage", 0) == false);
    CHECK(fees.hasAccess(standing(300), "safe_passage", 0));
}

TEST_CASE("leases and passes round-trip and reject hostile bytes")
{
    const CurrencyTable table = shipped();
    Wallet wallet = rich(table, 1000);
    const FactionStanding s = standing(100);
    AccessFees a = make();
    REQUIRE(a.payBribe(wallet, table, s, "safe_passage", 9) == FeeStatus::Ok);
    REQUIRE(a.payDues(wallet, table, s, "archonate", 7) == FeeStatus::Ok);

    const auto bytes = a.serialise();
    AccessFees b = make();
    CHECK(b.digest() != a.digest());
    REQUIRE(b.deserialise(bytes));
    CHECK(b.digest() == a.digest());
    CHECK(b.duesUntil("archonate") == 1007);
    CHECK(b.bribeUntil("safe_passage") == 209);

    const std::uint64_t before = b.digest();
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        CHECK_FALSE(b.deserialise(std::span<const std::uint8_t>(bytes.data(), cut)));
    }
    auto trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(b.deserialise(trailing));
    auto wrongVersion = bytes;
    wrongVersion[0] = 9;
    CHECK_FALSE(b.deserialise(wrongVersion));
    auto wrongCount = bytes;
    wrongCount[4] = 2;
    CHECK_FALSE(b.deserialise(wrongCount));
    auto wrongId = bytes; // the first lease's faction id starts after version (4), count (4), length (4)
    wrongId[12] = 'x';
    CHECK_FALSE(b.deserialise(wrongId));
    auto forever = bytes; // the first lease end follows "archonate" (9 bytes)
    for (std::size_t i = 0; i < 8; ++i) {
        forever[21 + i] = 0xFF;
    }
    CHECK_FALSE(b.deserialise(forever));
    CHECK(b.digest() == before);

    AccessFees empty = make({});
    CHECK_FALSE(empty.deserialise(bytes));
}
