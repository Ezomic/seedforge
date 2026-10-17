#include "ichor/data/data_set.h"
#include "ichor/economy/contract_board.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace ichor;

namespace {

BoardConfig config()
{
    BoardConfig c;
    c.periodTicks = 1000;
    c.slots = 6;
    c.maxActive = 3;
    c.maxPayoutsPerPeriod = 2;
    c.gracePeriods = 1;
    c.templates = {
        {ContractKind::Bounty, "raider", 1, 4, 10},
        {ContractKind::Delivery, "salt", 2, 8, 3},
    };
    c.issuers = {"archonate", "archaists"};
    return c;
}

ContractBoard board(std::uint64_t seed = 7, BoardConfig c = config())
{
    std::string error;
    auto b = ContractBoard::create(std::move(c), seed, error);
    REQUIRE(b.has_value());
    return std::move(*b);
}

CurrencyTable shipped()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto t = CurrencyTable::fromTable(*table, error);
    REQUIRE(t.has_value());
    return *t;
}

}

TEST_CASE("create rejects a bad config", "[contract_board]")
{
    const auto bad = [](auto edit) {
        BoardConfig c = config();
        edit(c);
        std::string error;
        const bool ok = ContractBoard::create(std::move(c), 1, error).has_value();
        return !ok && !error.empty();
    };
    CHECK(bad([](BoardConfig& c) { c.periodTicks = 0; }));
    CHECK(bad([](BoardConfig& c) { c.periodTicks = kMaxBoardPeriod + 1; }));
    CHECK(bad([](BoardConfig& c) { c.slots = 0; }));
    CHECK(bad([](BoardConfig& c) { c.slots = kMaxBoardSlots + 1; }));
    CHECK(bad([](BoardConfig& c) { c.maxActive = 0; }));
    CHECK(bad([](BoardConfig& c) { c.maxPayoutsPerPeriod = 0; }));
    CHECK(bad([](BoardConfig& c) { c.gracePeriods = kMaxGracePeriods + 1; }));
    CHECK(bad([](BoardConfig& c) { c.templates.clear(); }));
    CHECK(bad([](BoardConfig& c) { c.issuers.clear(); }));
    CHECK(bad([](BoardConfig& c) { c.issuers = {"a", "a"}; }));
    CHECK(bad([](BoardConfig& c) { c.issuers = {""}; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].target.clear(); }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].minQuantity = 0; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].minQuantity = 5; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].maxQuantity = kMaxContractQuantity + 1; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].unitPrice = 0; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].unitPrice = kMaxUnitPrice + 1; }));
    CHECK(bad([](BoardConfig& c) { c.templates[0].kind = static_cast<ContractKind>(9); }));
}

TEST_CASE("offers are deterministic, flat priced and honestly described", "[contract_board]")
{
    const ContractBoard a = board(7);
    CHECK(a.offersFor(3) == board(7).offersFor(3));
    CHECK(a.offersFor(3) != a.offersFor(4));
    CHECK(a.offersFor(3) != board(8).offersFor(3));
    CHECK(a.offersFor(kMaxPeriodIndex).empty());

    const auto offers = a.offersFor(3);
    REQUIRE(offers.size() == 6);
    std::set<std::uint64_t> ids;
    for (const Contract& c : offers) {
        ids.insert(c.id);
        const std::uint64_t unit = c.kind == ContractKind::Bounty ? 10 : 3;
        CHECK(c.reward == c.quantity * unit);
        CHECK(c.expiresAt == 5000);
        CHECK(describe(c).find("Pays " + std::to_string(c.reward) + " coin.") != std::string::npos);
    }
    CHECK(ids.size() == 6);
}

TEST_CASE("accept takes an offer once and respects the hold limit", "[contract_board]")
{
    ContractBoard b = board();
    REQUIRE(b.advanceTo(2500) == BoardStatus::Ok);
    CHECK(b.period() == 2);
    const auto offers = b.openOffers();
    REQUIRE(offers.size() == 6);

    CHECK(b.accept(offers[0].id) == BoardStatus::Ok);
    CHECK(b.accept(offers[0].id) == BoardStatus::AlreadyTaken);
    CHECK(b.openOffers().size() == 5);
    CHECK(b.accept(b.offersFor(1)[0].id) == BoardStatus::UnknownContract);
    CHECK(b.accept((2ull << 16) | 60) == BoardStatus::UnknownContract);
    CHECK(b.accept(offers[1].id) == BoardStatus::Ok);
    CHECK(b.accept(offers[2].id) == BoardStatus::Ok);
    CHECK(b.accept(offers[3].id) == BoardStatus::TooManyActive);
    CHECK(b.held().size() == 3);
}

TEST_CASE("abandoning does not reopen the offer, so it cannot be rerolled", "[contract_board]")
{
    ContractBoard b = board();
    const auto id = b.openOffers()[0].id;
    REQUIRE(b.accept(id) == BoardStatus::Ok);
    CHECK(b.abandon(id) == BoardStatus::Ok);
    CHECK(b.abandon(id) == BoardStatus::UnknownContract);
    CHECK(b.accept(id) == BoardStatus::AlreadyTaken);
    CHECK(b.openOffers().size() == 5);
}

TEST_CASE("payout is the flat reward and is capped per period", "[contract_board]")
{
    const CurrencyTable table = shipped();
    ContractBoard b = board();
    Wallet wallet;
    const auto offers = b.openOffers();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(b.accept(offers[i].id) == BoardStatus::Ok);
    }
    const auto first = b.complete(offers[0].id, wallet, table);
    CHECK(first.status == BoardStatus::Ok);
    CHECK(first.credited == offers[0].reward);
    CHECK(wallet.balance(CurrencySlot::Coin) == offers[0].reward);
    CHECK(b.complete(offers[0].id, wallet, table).status == BoardStatus::UnknownContract);
    CHECK(b.complete(offers[1].id, wallet, table).status == BoardStatus::Ok);

    const std::uint64_t before = wallet.balance(CurrencySlot::Coin);
    CHECK(b.complete(offers[2].id, wallet, table).status == BoardStatus::PayoutCapReached);
    CHECK(wallet.balance(CurrencySlot::Coin) == before);
    CHECK(b.held().size() == 1);

    REQUIRE(b.advanceTo(1000) == BoardStatus::Ok);
    CHECK(b.payoutsThisPeriod() == 0);
    CHECK(b.complete(offers[2].id, wallet, table).status == BoardStatus::Ok);
}

TEST_CASE("repeating the same job never pays more", "[contract_board]")
{
    const CurrencyTable table = shipped();
    BoardConfig c = config();
    c.maxPayoutsPerPeriod = 1;
    c.slots = 1;
    c.templates = {{ContractKind::Bounty, "raider", 2, 2, 10}};
    ContractBoard b = board(5, c);
    Wallet wallet;
    std::vector<std::uint64_t> pay;
    for (std::uint64_t p = 0; p < 20; ++p) {
        REQUIRE(b.advanceTo(p * 1000) == BoardStatus::Ok);
        const auto offer = b.openOffers()[0];
        REQUIRE(b.accept(offer.id) == BoardStatus::Ok);
        const auto done = b.complete(offer.id, wallet, table);
        REQUIRE(done.status == BoardStatus::Ok);
        pay.push_back(done.credited);
    }
    CHECK(*std::min_element(pay.begin(), pay.end()) == *std::max_element(pay.begin(), pay.end()));
    CHECK(wallet.balance(CurrencySlot::Coin) == 20 * 20);
}

TEST_CASE("unfinished contracts expire after the grace period", "[contract_board]")
{
    const CurrencyTable table = shipped();
    ContractBoard b = board();
    Wallet wallet;
    const auto id = b.openOffers()[0].id;
    REQUIRE(b.accept(id) == BoardStatus::Ok);
    REQUIRE(b.advanceTo(1999) == BoardStatus::Ok);
    CHECK(b.held().size() == 1);
    REQUIRE(b.advanceTo(2000) == BoardStatus::Ok);
    CHECK(b.held().empty());
    CHECK(b.complete(id, wallet, table).status == BoardStatus::UnknownContract);
    CHECK(wallet.balance(CurrencySlot::Coin) == 0);
}

TEST_CASE("a full wallet keeps the contract and spends no payout", "[contract_board]")
{
    const CurrencyTable table = shipped();
    ContractBoard b = board();
    Wallet wallet;
    REQUIRE(wallet.earn(table, CurrencySlot::Coin, table.def(CurrencySlot::Coin).cap, "loot").credited == table.def(CurrencySlot::Coin).cap);
    const auto id = b.openOffers()[0].id;
    REQUIRE(b.accept(id) == BoardStatus::Ok);
    CHECK(b.complete(id, wallet, table).status == BoardStatus::WalletFull);
    CHECK(b.held().size() == 1);
    CHECK(b.payoutsThisPeriod() == 0);
}

TEST_CASE("time cannot go back or past the id range", "[contract_board]")
{
    ContractBoard b = board();
    REQUIRE(b.advanceTo(5000) == BoardStatus::Ok);
    CHECK(b.advanceTo(4999) == BoardStatus::TimeInvalid);
    CHECK(b.advanceTo(std::numeric_limits<std::uint64_t>::max()) == BoardStatus::TimeInvalid);
    CHECK(b.advanceTo(kMaxPeriodIndex * 1000) == BoardStatus::TimeInvalid);
    CHECK(b.period() == 5);
}

TEST_CASE("state round-trips and hostile bytes are refused", "[contract_board]")
{
    const CurrencyTable table = shipped();
    ContractBoard b = board();
    Wallet wallet;
    REQUIRE(b.advanceTo(1200) == BoardStatus::Ok);
    const auto offers = b.openOffers();
    REQUIRE(b.accept(offers[0].id) == BoardStatus::Ok);
    REQUIRE(b.accept(offers[1].id) == BoardStatus::Ok);
    REQUIRE(b.complete(offers[0].id, wallet, table).status == BoardStatus::Ok);
    REQUIRE(b.advanceTo(1500) == BoardStatus::Ok);

    const auto bytes = b.serialise();
    const auto back = ContractBoard::deserialise(bytes, config(), 7);
    REQUIRE(back.has_value());
    CHECK(back->digest() == b.digest());
    CHECK(back->held() == b.held());
    CHECK(back->openOffers() == b.openOffers());
    CHECK(back->payoutsThisPeriod() == 1);

    CHECK_FALSE(ContractBoard::deserialise({}, config(), 7).has_value());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK_FALSE(ContractBoard::deserialise(std::span(bytes.data(), n), config(), 7).has_value());
    }
    auto extra = bytes;
    extra.push_back(0);
    CHECK_FALSE(ContractBoard::deserialise(extra, config(), 7).has_value());
    auto version = bytes;
    version[0] = 9;
    CHECK_FALSE(ContractBoard::deserialise(version, config(), 7).has_value());
    CHECK_FALSE(ContractBoard::deserialise(bytes, config(), 8).has_value());
    BoardConfig patched = config();
    patched.templates[0].unitPrice = 11;
    CHECK_FALSE(ContractBoard::deserialise(bytes, patched, 7).has_value());
    BoardConfig broken = config();
    broken.slots = 0;
    CHECK_FALSE(ContractBoard::deserialise(bytes, broken, 7).has_value());
}

TEST_CASE("every single-byte corruption either fails or yields a board that re-serialises", "[contract_board]")
{
    ContractBoard b = board();
    REQUIRE(b.advanceTo(1200) == BoardStatus::Ok);
    REQUIRE(b.accept(b.openOffers()[0].id) == BoardStatus::Ok);
    const auto bytes = b.serialise();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        for (const unsigned flip : {0x01u, 0x80u, 0xFFu}) {
            auto mutated = bytes;
            mutated[i] = static_cast<std::uint8_t>(mutated[i] ^ flip);
            const auto back = ContractBoard::deserialise(mutated, config(), 7);
            if (back) {
                CHECK(ContractBoard::deserialise(back->serialise(), config(), 7).has_value());
            }
        }
    }
}

TEST_CASE("a taken id from another period is refused", "[contract_board]")
{
    ContractBoard b = board();
    REQUIRE(b.advanceTo(1200) == BoardStatus::Ok);
    REQUIRE(b.accept(b.openOffers()[0].id) == BoardStatus::Ok);
    auto bytes = b.serialise();
    // version(4) fingerprint(8) tick(8) payouts(4) takenCount(4), then the taken id: flip a bit of its period
    const std::size_t takenId = 4 + 8 + 8 + 4 + 4;
    bytes[takenId + 2] ^= 0x01;
    CHECK_FALSE(ContractBoard::deserialise(bytes, config(), 7).has_value());
}
