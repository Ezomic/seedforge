#include "ichor/economy/merchant_service.h"

#include <algorithm>
#include <limits>

namespace ichor {

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

MerchantOffer* findOffer(std::span<MerchantOffer> offers, std::string_view good)
{
    const auto it = std::find_if(offers.begin(), offers.end(), [&](const MerchantOffer& o) { return o.good == good; });
    return it == offers.end() ? nullptr : &*it;
}

bool mulFits(std::uint64_t a, std::uint64_t b)
{
    return a == 0 || b <= kMax / a;
}

ServiceStatus fromTx(TxStatus s)
{
    switch (s) {
    case TxStatus::Ok: return ServiceStatus::Ok;
    case TxStatus::Insufficient: return ServiceStatus::Insufficient;
    case TxStatus::NotAllowed: return ServiceStatus::NotAllowed;
    }
    return ServiceStatus::InvalidInput;
}

}

TradeResult buyFromMerchant(std::span<MerchantOffer> offers, std::string_view good, std::uint64_t quantity, const CurrencyTable& table, Wallet& wallet)
{
    MerchantOffer* offer = findOffer(offers, good);
    if (offer == nullptr) {
        return {ServiceStatus::UnknownGood, 0};
    }
    if (quantity == 0) {
        return {ServiceStatus::ZeroQuantity, 0};
    }
    if (offer->price == 0) {
        return {ServiceStatus::InvalidInput, 0};
    }
    if (quantity > offer->stock) {
        return {ServiceStatus::OutOfStock, 0};
    }
    if (!mulFits(offer->price, quantity)) {
        return {ServiceStatus::Overflow, 0};
    }
    const std::uint64_t total = offer->price * quantity;
    const TxStatus tx = wallet.spend(table, CurrencySlot::Coin, total, "trade");
    if (tx != TxStatus::Ok) {
        return {fromTx(tx), 0};
    }
    offer->stock -= quantity;
    return {ServiceStatus::Ok, total};
}

TradeResult sellToMerchant(std::span<MerchantOffer> offers, std::string_view good, std::uint64_t quantity, const CurrencyTable& table, Wallet& wallet)
{
    MerchantOffer* offer = findOffer(offers, good);
    if (offer == nullptr) {
        return {ServiceStatus::UnknownGood, 0};
    }
    if (quantity == 0) {
        return {ServiceStatus::ZeroQuantity, 0};
    }
    if (!mulFits(offer->buyback, quantity) || quantity > kMax - offer->stock) {
        return {ServiceStatus::Overflow, 0};
    }
    const std::uint64_t total = offer->buyback * quantity;
    const std::uint64_t cap = table.def(CurrencySlot::Coin).cap;
    if (!table.canEarnFrom(CurrencySlot::Coin, "trade")) {
        return {ServiceStatus::NotAllowed, 0};
    }
    if (total > cap - std::min(cap, wallet.balance(CurrencySlot::Coin))) {
        return {ServiceStatus::WalletFull, 0};
    }
    const EarnResult earned = wallet.earn(table, CurrencySlot::Coin, total, "trade");
    if (earned.status != TxStatus::Ok) {
        return {fromTx(earned.status), 0};
    }
    offer->stock += quantity;
    return {ServiceStatus::Ok, earned.credited};
}

RepairQuote quoteRepair(const WeaponWear& weapon, const RepairRules& rules)
{
    if (weapon.maxDurability == 0 || weapon.maxDurability > kMaxDurability || weapon.durability > weapon.maxDurability || weapon.value > kMaxGoodPrice || rules.fullRepairPermille > kMaxPermille) {
        return {ServiceStatus::InvalidInput, 0, 0};
    }
    RepairQuote quote;
    quote.missing = weapon.maxDurability - weapon.durability;
    if (quote.missing == 0) {
        return quote;
    }
    // missing <= 1e5, value <= 1e9, permille <= 1e5: the product is at most 1e19, under 2^64.
    const std::uint64_t numerator = static_cast<std::uint64_t>(quote.missing) * weapon.value * rules.fullRepairPermille;
    const std::uint64_t denominator = static_cast<std::uint64_t>(weapon.maxDurability) * 1000;
    const std::uint64_t cost = numerator / denominator + (numerator % denominator != 0 ? 1 : 0);
    quote.cost = std::max<std::uint64_t>(cost, 1);
    return quote;
}

RepairQuote repairWeapon(WeaponWear& weapon, const RepairRules& rules, const CurrencyTable& table, Wallet& wallet)
{
    RepairQuote quote = quoteRepair(weapon, rules);
    if (quote.status != ServiceStatus::Ok || quote.missing == 0) {
        return quote;
    }
    const TxStatus tx = wallet.spend(table, CurrencySlot::Coin, quote.cost, "repair");
    if (tx != TxStatus::Ok) {
        quote.status = fromTx(tx);
        return quote;
    }
    weapon.durability = weapon.maxDurability;
    return quote;
}

}
