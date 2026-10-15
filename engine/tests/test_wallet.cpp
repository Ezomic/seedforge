#include "ichor/data/data_set.h"
#include "ichor/economy/wallet.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

std::filesystem::path uniqueRoot(const std::string& tag)
{
    const auto root = std::filesystem::path(ICHOR_TEST_OUTPUT) / ("wallet-" + tag + "-" + std::to_string(ICHOR_PID));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "currency");
    return root;
}

std::optional<CurrencyTable> tableFromJson(const std::string& tag, const std::string& json, std::string& error)
{
    const auto root = uniqueRoot(tag);
    std::ofstream(root / "currency" / "currencies.json", std::ios::binary) << json;
    DataSet data;
    if (!data.loadDirectory(root)) {
        error = data.lastError();
        return std::nullopt;
    }
    const DataTable* table = data.table("currency");
    if (table == nullptr) {
        error = "no currency table";
        return std::nullopt;
    }
    return CurrencyTable::fromTable(*table, error);
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

const std::string kCoin = R"({"id": "coin", "name": "Coin", "symbol": "c", "earn": ["loot"], "spend": ["trade"], "cap": 1000})";

std::string hardJson(const std::string& fields)
{
    return "[" + kCoin + ", {\"id\": \"hard\", " + fields + "}]";
}

const std::string kGoodHard = R"("name": "Charge", "symbol": "ch", "earn": ["loot"], "spend": ["power"], "cap": 50)";

}

TEST_CASE("the shipped table defines coin and a hard currency from data")
{
    const CurrencyTable table = shipped();
    CHECK(table.def(CurrencySlot::Coin).id == "coin");
    CHECK(table.def(CurrencySlot::Coin).name == "Coin");
    CHECK(table.def(CurrencySlot::Hard).id == "hard");
    CHECK(table.def(CurrencySlot::Hard).name == "Charge");
    CHECK(table.canSpendOn(CurrencySlot::Hard, "power"));
    CHECK_FALSE(table.canSpendOn(CurrencySlot::Coin, "power"));
}

TEST_CASE("one data swap changes the hard currency and nothing else")
{
    std::string error;
    const auto charge = tableFromJson("charge", hardJson(kGoodHard), error);
    const auto vitality = tableFromJson("vitality", hardJson(R"("name": "Vitality", "symbol": "vit", "earn": [], "spend": ["power", "offering"], "cap": 7)"), error);
    REQUIRE(charge.has_value());
    REQUIRE(vitality.has_value());

    CHECK(charge->def(CurrencySlot::Hard).name == "Charge");
    CHECK(vitality->def(CurrencySlot::Hard).name == "Vitality");
    CHECK(charge->def(CurrencySlot::Coin).name == vitality->def(CurrencySlot::Coin).name);

    Wallet a;
    Wallet b;
    CHECK(a.earn(*charge, CurrencySlot::Hard, 10, "loot").credited == 10);
    CHECK(b.earn(*vitality, CurrencySlot::Hard, 10, "loot").status == TxStatus::NotAllowed);
    CHECK(a.earn(*charge, CurrencySlot::Hard, 100, "loot").credited == 40);
    CHECK(a.spend(*vitality, CurrencySlot::Hard, 1, "offering") == TxStatus::Ok);
}

TEST_CASE("earn and spend move the balance and respect the rules")
{
    const CurrencyTable table = shipped();
    Wallet wallet;
    CHECK(wallet.earn(table, CurrencySlot::Coin, 30, "loot").credited == 30);
    CHECK(wallet.balance(CurrencySlot::Coin) == 30);
    CHECK(wallet.balance(CurrencySlot::Hard) == 0);
    CHECK(wallet.spend(table, CurrencySlot::Coin, 10, "trade") == TxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Coin) == 20);
    CHECK(wallet.canAfford(CurrencySlot::Coin, 20));
    CHECK_FALSE(wallet.canAfford(CurrencySlot::Coin, 21));

    CHECK(wallet.earn(table, CurrencySlot::Coin, 5, "theft").status == TxStatus::NotAllowed);
    CHECK(wallet.spend(table, CurrencySlot::Coin, 5, "power") == TxStatus::NotAllowed);
    CHECK(wallet.balance(CurrencySlot::Coin) == 20);
}

TEST_CASE("adversarial amounts never produce a negative or wrapped balance")
{
    const CurrencyTable table = shipped();
    const std::uint64_t coinCap = table.def(CurrencySlot::Coin).cap;
    Wallet wallet;

    SECTION("zero is an allowed no-op, but only for a permitted source or sink")
    {
        const Wallet before = wallet;
        CHECK(wallet.earn(table, CurrencySlot::Coin, 0, "loot").status == TxStatus::Ok);
        CHECK(wallet.earn(table, CurrencySlot::Coin, 0, "theft").status == TxStatus::NotAllowed);
        CHECK(wallet.spend(table, CurrencySlot::Coin, 0, "trade") == TxStatus::Ok);
        CHECK(wallet.canAfford(CurrencySlot::Coin, 0));
        CHECK(wallet == before);
    }
    SECTION("spending more than owned is refused and changes nothing")
    {
        (void)wallet.earn(table, CurrencySlot::Coin, 5, "loot");
        const Wallet before = wallet;
        CHECK(wallet.spend(table, CurrencySlot::Coin, 6, "trade") == TxStatus::Insufficient);
        CHECK(wallet.spend(table, CurrencySlot::Coin, kMax, "trade") == TxStatus::Insufficient);
        CHECK(wallet.spend(table, CurrencySlot::Coin, 1, "trade") == TxStatus::Ok);
        CHECK(wallet.spend(table, CurrencySlot::Coin, 5, "trade") == TxStatus::Insufficient);
        CHECK(wallet.balance(CurrencySlot::Coin) == 4);
        CHECK_FALSE(wallet == before);
    }
    SECTION("a spend from an empty wallet is refused")
    {
        CHECK(wallet.spend(table, CurrencySlot::Hard, 1, "power") == TxStatus::Insufficient);
        CHECK(wallet.balance(CurrencySlot::Hard) == 0);
    }
    SECTION("a huge earn clips to the cap instead of wrapping")
    {
        CHECK(wallet.earn(table, CurrencySlot::Coin, kMax, "loot").credited == coinCap);
        CHECK(wallet.balance(CurrencySlot::Coin) == coinCap);
        CHECK(wallet.earn(table, CurrencySlot::Coin, kMax, "loot").credited == 0);
        CHECK(wallet.earn(table, CurrencySlot::Coin, 1, "loot").credited == 0);
        CHECK(wallet.balance(CurrencySlot::Coin) == coinCap);
        CHECK(wallet.spend(table, CurrencySlot::Coin, coinCap, "trade") == TxStatus::Ok);
        CHECK(wallet.balance(CurrencySlot::Coin) == 0);
    }
    SECTION("the two balances are independent")
    {
        (void)wallet.earn(table, CurrencySlot::Hard, kMax, "loot");
        CHECK(wallet.balance(CurrencySlot::Hard) == table.def(CurrencySlot::Hard).cap);
        CHECK(wallet.balance(CurrencySlot::Coin) == 0);
        (void)wallet.earn(table, CurrencySlot::Coin, 4, "loot");
        CHECK(wallet.spend(table, CurrencySlot::Coin, 4, "trade") == TxStatus::Ok);
        CHECK(wallet.balance(CurrencySlot::Hard) == table.def(CurrencySlot::Hard).cap);
    }
}

TEST_CASE("a cap of 2^53, the largest data can name, still cannot overflow")
{
    std::string error;
    const auto table = tableFromJson("bigcap", hardJson(R"("name": "Ore", "symbol": "o", "earn": ["loot"], "spend": ["power"], "cap": 9007199254740992)"), error);
    INFO(error);
    REQUIRE(table.has_value());
    Wallet wallet;
    CHECK(wallet.earn(*table, CurrencySlot::Hard, kMax, "loot").credited == kMaxCurrencyCap);
    CHECK(wallet.earn(*table, CurrencySlot::Hard, kMax, "loot").credited == 0);
    CHECK(wallet.balance(CurrencySlot::Hard) == kMaxCurrencyCap);
}

TEST_CASE("the digest is deterministic, sensitive to every balance and pinned")
{
    const CurrencyTable table = shipped();
    Wallet a;
    Wallet b;
    CHECK(a.digest() == b.digest());

    (void)a.earn(table, CurrencySlot::Coin, 7, "loot");
    CHECK(a.digest() != b.digest());
    (void)b.earn(table, CurrencySlot::Coin, 7, "quest");
    CHECK(a.digest() == b.digest());

    Wallet coinOnly;
    Wallet hardOnly;
    (void)coinOnly.earn(table, CurrencySlot::Coin, 7, "loot");
    (void)hardOnly.earn(table, CurrencySlot::Hard, 7, "loot");
    CHECK(coinOnly.digest() != hardOnly.digest());

    CHECK(Wallet{}.digest() == 0x0C593F1B0D139064ull);
}

TEST_CASE("serialisation is little-endian and round-trips")
{
    const CurrencyTable table = shipped();
    Wallet wallet;
    (void)wallet.earn(table, CurrencySlot::Coin, 0x0102, "loot");
    (void)wallet.earn(table, CurrencySlot::Hard, 3, "loot");

    const std::vector<std::uint8_t> bytes = wallet.serialise();
    REQUIRE(bytes.size() == 4 + 8 + 8);
    CHECK(bytes[0] == 1);
    CHECK(bytes[4] == 0x02);
    CHECK(bytes[5] == 0x01);
    CHECK(bytes[12] == 3);

    const auto restored = Wallet::deserialise(bytes, table);
    REQUIRE(restored.has_value());
    CHECK(*restored == wallet);
    CHECK(restored->digest() == wallet.digest());
}

TEST_CASE("corrupt wallet bytes are rejected")
{
    const CurrencyTable table = shipped();
    Wallet wallet;
    (void)wallet.earn(table, CurrencySlot::Coin, 5, "loot");
    const std::vector<std::uint8_t> good = wallet.serialise();

    SECTION("empty and every truncation")
    {
        for (std::size_t length = 0; length < good.size(); ++length) {
            CHECK_FALSE(Wallet::deserialise(std::vector<std::uint8_t>(good.begin(), good.begin() + length), table).has_value());
        }
    }
    SECTION("trailing bytes")
    {
        std::vector<std::uint8_t> bytes = good;
        bytes.push_back(0);
        CHECK_FALSE(Wallet::deserialise(bytes, table).has_value());
    }
    SECTION("an unknown version")
    {
        std::vector<std::uint8_t> bytes = good;
        bytes[0] = 2;
        CHECK_FALSE(Wallet::deserialise(bytes, table).has_value());
    }
    SECTION("a balance above the cap, in either slot")
    {
        for (const std::size_t first : {std::size_t{4}, std::size_t{12}}) {
            std::vector<std::uint8_t> bytes = good;
            for (std::size_t i = first; i < first + 8; ++i) {
                bytes[i] = 0xFF;
            }
            CHECK_FALSE(Wallet::deserialise(bytes, table).has_value());
        }
    }
    SECTION("a balance of exactly the cap is accepted and one more is not")
    {
        Wallet full;
        (void)full.earn(table, CurrencySlot::Coin, kMax, "loot");
        (void)full.earn(table, CurrencySlot::Hard, kMax, "loot");
        const auto restored = Wallet::deserialise(full.serialise(), table);
        REQUIRE(restored.has_value());
        CHECK(*restored == full);

        std::vector<std::uint8_t> bytes = full.serialise();
        ++bytes[12];
        CHECK_FALSE(Wallet::deserialise(bytes, table).has_value());
    }
}

TEST_CASE("bad currency tables are rejected with a message naming the currency")
{
    struct Case {
        const char* tag;
        std::string json;
        const char* message;
    };
    const std::vector<Case> cases = {
        {"missing", "[" + kCoin + "]", "needs both"},
        {"nocoin", "[{\"id\": \"hard\", " + kGoodHard + "}]", "needs both"},
        {"notlist", hardJson(R"("name": "C", "symbol": "c", "earn": "loot", "spend": [], "cap": 5)"), "'hard': \"earn\""},
        {"third", "[" + kCoin + ", {\"id\": \"gem\", " + kGoodHard + "}]", "only currencies"},
        {"noname", hardJson(R"("name": "", "symbol": "ch", "earn": [], "spend": [], "cap": 5)"), "'hard': \"name\""},
        {"nosymbol", hardJson(R"("name": "C", "earn": [], "spend": [], "cap": 5)"), "'hard': \"symbol\""},
        {"dupearn", hardJson(R"("name": "C", "symbol": "c", "earn": ["loot", "loot"], "spend": [], "cap": 5)"), "'hard': \"earn\""},
        {"emptyspend", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [""], "cap": 5)"), "'hard': \"spend\""},
        {"zerocap", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [], "cap": 0)"), "'hard': \"cap\""},
        {"negcap", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [], "cap": -4)"), "'hard': \"cap\""},
        {"fraccap", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [], "cap": 2.5)"), "'hard': \"cap\""},
        {"hugecap", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [], "cap": 1e19)"), "'hard': \"cap\""},
        {"unknown", hardJson(R"("name": "C", "symbol": "c", "earn": [], "spend": [], "cap": 5, "weight": 1)"), "unknown field"},
    };
    for (const Case& c : cases) {
        std::string error;
        const auto table = tableFromJson(c.tag, c.json, error);
        INFO(c.tag << ": " << error);
        CHECK_FALSE(table.has_value());
        CHECK(error.find(c.message) != std::string::npos);
    }
}
