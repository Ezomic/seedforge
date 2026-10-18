#include "ichor/data/data_set.h"
#include "ichor/economy/merchant_service.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

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

Wallet fundedWallet(const CurrencyTable& table, std::uint64_t coin)
{
    Wallet wallet;
    REQUIRE(wallet.earn(table, CurrencySlot::Coin, coin, "loot").status == TxStatus::Ok);
    return wallet;
}

std::vector<MerchantOffer> shelf()
{
    return {
        {"grain", Scarcity::Normal, 10, 5, 20},
        {"silver", Scarcity::Normal, 100, 50, 3},
        {"gold", Scarcity::Normal, kMax / 2, 1, 10},
    };
}

}

TEST_CASE("buying debits the flat price and takes stock", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 100);
    auto offers = shelf();
    const TradeResult r = buyFromMerchant(offers, "grain", 4, table, wallet);
    CHECK(r.status == ServiceStatus::Ok);
    CHECK(r.total == 40);
    CHECK(wallet.balance(CurrencySlot::Coin) == 60);
    CHECK(offers[0].stock == 16);
}

TEST_CASE("a refused buy changes neither wallet nor stock", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 150);
    auto offers = shelf();

    CHECK(buyFromMerchant(offers, "silver", 1, table, wallet).status == ServiceStatus::Ok);
    CHECK(buyFromMerchant(offers, "silver", 1, table, wallet).status == ServiceStatus::Insufficient);
    CHECK(buyFromMerchant(offers, "nothing", 1, table, wallet).status == ServiceStatus::UnknownGood);
    CHECK(buyFromMerchant(offers, "grain", 0, table, wallet).status == ServiceStatus::ZeroQuantity);
    CHECK(buyFromMerchant(offers, "grain", 21, table, wallet).status == ServiceStatus::OutOfStock);
    CHECK(wallet.balance(CurrencySlot::Coin) == 50);
    CHECK(offers[0].stock == 20);
    CHECK(offers[1].stock == 2);
}

TEST_CASE("an overflowing total is rejected, not wrapped", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 100);
    auto offers = shelf();
    CHECK(buyFromMerchant(offers, "gold", 3, table, wallet).status == ServiceStatus::Overflow);
    CHECK(wallet.balance(CurrencySlot::Coin) == 100);
    CHECK(offers[2].stock == 10);
    offers[0].stock = kMax;
    CHECK(sellToMerchant(offers, "grain", 1, table, wallet).status == ServiceStatus::Overflow);
}

TEST_CASE("selling pays the buyback and grows stock", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet;
    auto offers = shelf();
    const TradeResult r = sellToMerchant(offers, "silver", 2, table, wallet);
    CHECK(r.status == ServiceStatus::Ok);
    CHECK(r.total == 100);
    CHECK(wallet.balance(CurrencySlot::Coin) == 100);
    CHECK(offers[1].stock == 5);
    CHECK(sellToMerchant(offers, "silver", 0, table, wallet).status == ServiceStatus::ZeroQuantity);
}

TEST_CASE("repair cost scales with wear and weapon value", "[merchant_service][repair]")
{
    const RepairRules rules;
    const WeaponWear fresh{100, 100, 1000};
    const WeaponWear half{50, 100, 1000};
    const WeaponWear broken{0, 100, 1000};
    CHECK(quoteRepair(fresh, rules).cost == 0);
    CHECK(quoteRepair(half, rules).cost == 125);
    CHECK(quoteRepair(broken, rules).cost == 250);
    CHECK(quoteRepair(broken, rules).missing == 100);
    CHECK(quoteRepair(broken, rules).cost > quoteRepair(half, rules).cost);
}

TEST_CASE("any wear costs at least one coin and cost rounds up", "[merchant_service][repair]")
{
    const RepairRules rules;
    CHECK(quoteRepair({99, 100, 0}, rules).cost == 1);
    CHECK(quoteRepair({99, 100, 1}, rules).cost == 1);
    CHECK(quoteRepair({99999, 100000, 7}, rules).cost == 1);
    CHECK(quoteRepair({0, 3, 10}, RepairRules{333}).cost == 4);
}

TEST_CASE("repair math survives extreme inputs", "[merchant_service][repair]")
{
    const RepairRules worst{kMaxPermille};
    const RepairQuote q = quoteRepair({0, kMaxDurability, kMaxGoodPrice}, worst);
    CHECK(q.status == ServiceStatus::Ok);
    CHECK(q.cost == kMaxGoodPrice * kMaxPermille / 1000);
    CHECK(quoteRepair({0, 0, 1}, {}).status == ServiceStatus::InvalidInput);
    CHECK(quoteRepair({5, 4, 1}, {}).status == ServiceStatus::InvalidInput);
    CHECK(quoteRepair({0, kMaxDurability + 1, 1}, {}).status == ServiceStatus::InvalidInput);
    CHECK(quoteRepair({0, 10, kMaxGoodPrice + 1}, {}).status == ServiceStatus::InvalidInput);
    CHECK(quoteRepair({0, 10, 1}, RepairRules{kMaxPermille + 1}).status == ServiceStatus::InvalidInput);
    CHECK(quoteRepair({0, 10, kMax}, {}).status == ServiceStatus::InvalidInput);
}

TEST_CASE("repair pays the repair sink and restores durability", "[merchant_service][repair]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 1000);
    WeaponWear weapon{0, 100, 1000};
    const RepairQuote done = repairWeapon(weapon, {}, table, wallet);
    CHECK(done.status == ServiceStatus::Ok);
    CHECK(weapon.durability == 100);
    CHECK(wallet.balance(CurrencySlot::Coin) == 1000 - done.cost);

    const RepairQuote again = repairWeapon(weapon, {}, table, wallet);
    CHECK(again.cost == 0);
    CHECK(wallet.balance(CurrencySlot::Coin) == 1000 - done.cost);
}

TEST_CASE("an unaffordable repair leaves weapon and wallet alone", "[merchant_service][repair]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 10);
    WeaponWear weapon{0, 100, 1000};
    const RepairQuote r = repairWeapon(weapon, {}, table, wallet);
    CHECK(r.status == ServiceStatus::Insufficient);
    CHECK(weapon.durability == 0);
    CHECK(wallet.balance(CurrencySlot::Coin) == 10);
}

TEST_CASE("a sale the wallet cap would clip is refused whole", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    const std::uint64_t cap = table.def(CurrencySlot::Coin).cap;
    Wallet wallet = fundedWallet(table, cap - 60);
    auto offers = shelf();
    CHECK(sellToMerchant(offers, "silver", 1, table, wallet).status == ServiceStatus::Ok);
    CHECK(sellToMerchant(offers, "silver", 1, table, wallet).status == ServiceStatus::WalletFull);
    CHECK(wallet.balance(CurrencySlot::Coin) == cap - 60 + 50);
    CHECK(offers[1].stock == 4);
}

TEST_CASE("selling overflow, unknown goods and free offers are refused", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 100);
    auto offers = shelf();
    CHECK(sellToMerchant(offers, "nothing", 1, table, wallet).status == ServiceStatus::UnknownGood);
    CHECK(sellToMerchant(offers, "silver", kMax, table, wallet).status == ServiceStatus::Overflow);
    offers[0].price = 0;
    CHECK(buyFromMerchant(offers, "grain", 1, table, wallet).status == ServiceStatus::InvalidInput);
    CHECK(wallet.balance(CurrencySlot::Coin) == 100);
}

TEST_CASE("buying the whole stock works and one more does not", "[merchant_service]")
{
    const CurrencyTable table = shipped();
    Wallet wallet = fundedWallet(table, 1000);
    auto offers = shelf();
    CHECK(buyFromMerchant(offers, "grain", 20, table, wallet).total == 200);
    CHECK(offers[0].stock == 0);
    CHECK(buyFromMerchant(offers, "grain", 1, table, wallet).status == ServiceStatus::OutOfStock);
}
