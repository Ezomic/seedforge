#include "ichor/data/data_set.h"
#include "ichor/economy/wallet.h"
#include "ichor/progression/respec.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string readData(const char* name)
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / name, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<Capability> shippedCapabilities()
{
    std::vector<Capability> table;
    std::vector<std::string> problems;
    REQUIRE(parseCapabilities("capabilities.json", readData("capabilities.json"), table, problems));
    return table;
}

std::vector<RespecService> shippedServices()
{
    std::vector<RespecService> services;
    std::vector<std::string> problems;
    INFO((problems.empty() ? std::string() : problems.front()));
    REQUIRE(parseRespecServices("respec.json", readData("respec.json"), services, problems));
    return services;
}

CurrencyTable shippedCurrencies()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    REQUIRE(result.has_value());
    return *result;
}

const RespecService& service(const std::vector<RespecService>& all, const std::string& id)
{
    for (const RespecService& s : all) {
        if (s.id == id) {
            return s;
        }
    }
    FAIL("no service " << id);
    return all.front();
}

struct World {
    std::vector<Capability> table = shippedCapabilities();
    std::vector<RespecService> services = shippedServices();
    CurrencyTable currencies = shippedCurrencies();
    CapabilityUnlocks unlocks{table};
    Wallet wallet;

    World()
    {
        for (const char* id : {"swim", "dive", "climb", "descend"}) {
            REQUIRE(unlocks.unlock(id) == UnlockResult::Unlocked);
        }
    }

    void fund(CurrencySlot slot, std::uint64_t amount) { REQUIRE(wallet.earn(currencies, slot, amount, "loot").status == TxStatus::Ok); }
};

std::vector<std::string> parseProblems(const std::string& text)
{
    std::vector<RespecService> out;
    std::vector<std::string> problems;
    const bool ok = parseRespecServices("r.json", text, out, problems);
    REQUIRE(ok == problems.empty());
    return problems;
}

std::string record(const std::string& overrides = "")
{
    std::string text = R"({"id": "t", "name": "T", "currency": "coin", "sink": "trade", "base_cost": 1, "per_capability_cost": 1, "refund_per_capability": 0, "min_travel": 0)";
    return text + overrides + "}";
}

}

TEST_CASE("the shipped respec services parse and name real sinks", "[respec]")
{
    World w;
    REQUIRE(w.services.size() >= 2);
    for (const RespecService& s : w.services) {
        INFO(s.id);
        CHECK(w.currencies.canSpendOn(s.currency, s.sink));
    }
}

TEST_CASE("respec charges base plus per capability, refunds reduce it, and the removed set closes over dependents", "[respec]")
{
    World w;
    const RespecService& trainer = service(w.services, "trainer");
    w.fund(CurrencySlot::Coin, 10000);
    const RespecQuote quote = respec(trainer, w.table, w.unlocks, {"swim"}, w.wallet, w.currencies, 10);
    REQUIRE(quote.status == RespecStatus::Done);
    CHECK(quote.removed == std::vector<std::string>{"dive", "swim"});
    CHECK(quote.cost == trainer.baseCost + 2 * trainer.perCapabilityCost);
    CHECK(w.wallet.balance(CurrencySlot::Coin) == 10000 - quote.cost);
    CHECK_FALSE(w.unlocks.can("swim"));
    CHECK_FALSE(w.unlocks.can("dive"));
    CHECK(w.unlocks.can("climb"));
    CHECK(w.unlocks.can("descend"));
    CHECK(w.unlocks.unlock("dive") == UnlockResult::PrerequisiteUnmet);
    CHECK(w.unlocks.unlock("swim") == UnlockResult::Unlocked);
}

TEST_CASE("the refund discount lowers the price and never goes below zero", "[respec]")
{
    World w;
    const RespecService& ritual = service(w.services, "ritual");
    REQUIRE(ritual.refundPerCapability > 0);
    w.fund(CurrencySlot::Hard, 1000);
    CHECK(quoteRespec(ritual, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 100).cost
        == ritual.baseCost + 2 * ritual.perCapabilityCost - 2 * ritual.refundPerCapability);

    RespecService generous = ritual;
    generous.refundPerCapability = 1000000;
    const RespecQuote free = quoteRespec(generous, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 100);
    CHECK(free.cost == 0);
    CHECK(free.status == RespecStatus::Done);
    const auto before = w.wallet.digest();
    CHECK(respec(generous, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 100).status == RespecStatus::Done);
    CHECK(w.wallet.digest() == before);
    CHECK_FALSE(w.unlocks.can("climb"));
}

TEST_CASE("a refused respec changes nothing", "[respec]")
{
    World w;
    const RespecService& trainer = service(w.services, "trainer");
    const auto unlockDigest = w.unlocks.digest();
    const auto walletDigest = w.wallet.digest();

    SECTION("too poor")
    {
        CHECK(respec(trainer, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 10).status == RespecStatus::Insufficient);
    }
    SECTION("one coin short of the price")
    {
        w.fund(CurrencySlot::Coin, trainer.baseCost + 2 * trainer.perCapabilityCost - 1);
        const auto funded = w.wallet.digest();
        CHECK(respec(trainer, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 10).status == RespecStatus::Insufficient);
        CHECK(w.wallet.digest() == funded);
        CHECK(w.unlocks.digest() == unlockDigest);
        return;
    }
    SECTION("not far enough away")
    {
        w.fund(CurrencySlot::Coin, 100000);
        const auto funded = w.wallet.digest();
        CHECK(respec(trainer, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, trainer.minTravel - 1).status == RespecStatus::TooClose);
        CHECK(w.wallet.digest() == funded);
        CHECK(w.unlocks.digest() == unlockDigest);
        return;
    }
    SECTION("unknown, locked, empty and repeated targets")
    {
        w.fund(CurrencySlot::Coin, 100000);
        const auto funded = w.wallet.digest();
        CHECK(respec(trainer, w.table, w.unlocks, {"nope"}, w.wallet, w.currencies, 10).status == RespecStatus::UnknownCapability);
        CHECK(respec(trainer, w.table, w.unlocks, {"boat"}, w.wallet, w.currencies, 10).status == RespecStatus::NotUnlocked);
        CHECK(respec(trainer, w.table, w.unlocks, {}, w.wallet, w.currencies, 10).status == RespecStatus::NothingToRespec);
        CHECK(respec(trainer, w.table, w.unlocks, {"climb", "nope"}, w.wallet, w.currencies, 10).status == RespecStatus::UnknownCapability);
        CHECK(w.wallet.digest() == funded);
        CHECK(w.unlocks.digest() == unlockDigest);
        return;
    }
    SECTION("a sink the currency does not allow")
    {
        w.fund(CurrencySlot::Coin, 100000);
        RespecService bad = trainer;
        bad.sink = "power";
        const auto funded = w.wallet.digest();
        CHECK(respec(bad, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 10).status == RespecStatus::SinkNotAllowed);
        CHECK(w.wallet.digest() == funded);
    }
    CHECK(w.unlocks.digest() == unlockDigest);
}

TEST_CASE("duplicate targets are charged once", "[respec]")
{
    World w;
    const RespecService& trainer = service(w.services, "trainer");
    const RespecQuote once = quoteRespec(trainer, w.table, w.unlocks, {"climb"}, w.wallet, w.currencies, 10);
    const RespecQuote twice = quoteRespec(trainer, w.table, w.unlocks, {"climb", "climb", "descend"}, w.wallet, w.currencies, 10);
    CHECK(once.removed == twice.removed);
    CHECK(once.cost == twice.cost);
}

TEST_CASE("a hostile price cannot wrap the cost", "[respec]")
{
    World w;
    RespecService huge = service(w.services, "trainer");
    huge.baseCost = std::numeric_limits<std::uint64_t>::max() / 2;
    huge.perCapabilityCost = kMaxCurrencyCap;
    const RespecQuote quote = quoteRespec(huge, w.table, w.unlocks, {"swim", "climb"}, w.wallet, w.currencies, 100);
    CHECK(quote.status == RespecStatus::Insufficient);
    CHECK(quote.cost > kMaxCurrencyCap);
}

TEST_CASE("respec service parsing rejects bad data without throwing", "[respec]")
{
    CHECK(parseProblems("[" + record() + "]").empty());
    CHECK_FALSE(parseProblems("").empty());
    CHECK_FALSE(parseProblems("{}").empty());
    CHECK_FALSE(parseProblems("[1]").empty());
    CHECK_FALSE(parseProblems(std::string(300000, ' ')).empty());
    CHECK_FALSE(parseProblems(std::string(500, '[') + std::string(500, ']')).empty());
    const std::string escapedQuote = R"(["\"", )";
    CHECK_FALSE(parseProblems(escapedQuote + std::string(500, '[') + std::string(500, ']') + "]").empty());
    CHECK_FALSE(parseProblems("[" + record() + "," + record() + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "base_cost": -1)") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "base_cost": 1.5)") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "base_cost": 1e300)") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "base_cost": 9007199254740993)") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "base_cost": "5")") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "currency": "gold")") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "id": "Bad Id")") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record(R"(, "name": "a — b")") + "]").empty());
    CHECK_FALSE(parseProblems(R"([{"id": "t"}])").empty());
}
