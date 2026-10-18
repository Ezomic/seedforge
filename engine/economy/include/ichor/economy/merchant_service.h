#pragma once

#include "ichor/economy/merchant.h"
#include "ichor/economy/wallet.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace ichor {

// Doc 06 (Merchants, Verdicts): a merchant resolves flat-priced trades against
// the wallet, and a weapon's wear is a repair bill, never breakage. All
// arithmetic is checked integer math, so no price can wrap or go negative.

enum class ServiceStatus : std::uint8_t {
    Ok,
    UnknownGood,
    ZeroQuantity,
    OutOfStock,
    Insufficient,
    NotAllowed,
    Overflow,
    WalletFull,
    InvalidInput,
};

struct TradeResult {
    ServiceStatus status = ServiceStatus::Ok;
    std::uint64_t total = 0; // coin paid (buy) or credited (sell)
};

// Buys `quantity` units at the offer's flat price. All or nothing: a refused
// buy changes neither the wallet nor the stock.
[[nodiscard]] TradeResult buyFromMerchant(std::span<MerchantOffer> offers, std::string_view good, std::uint64_t quantity, const CurrencyTable& table, Wallet& wallet);

// Sells `quantity` units at the buyback price; the merchant's stock grows.
// All or nothing: a sale the wallet cap would clip is refused as WalletFull.
[[nodiscard]] TradeResult sellToMerchant(std::span<MerchantOffer> offers, std::string_view good, std::uint64_t quantity, const CurrencyTable& table, Wallet& wallet);

constexpr std::uint32_t kMaxDurability = 100000;

struct WeaponWear {
    std::uint32_t durability = 0;    // 0 to maxDurability
    std::uint32_t maxDurability = 0; // 1 to kMaxDurability
    std::uint64_t value = 0;         // what the weapon is worth in coin, 0 to kMaxGoodPrice
};

struct RepairRules {
    // Share of the weapon's value a full repair from zero costs, in permille.
    std::uint32_t fullRepairPermille = 250;
};

struct RepairQuote {
    ServiceStatus status = ServiceStatus::Ok;
    std::uint32_t missing = 0; // durability points to restore
    std::uint64_t cost = 0;    // 0 when nothing is missing, otherwise at least 1
};

[[nodiscard]] RepairQuote quoteRepair(const WeaponWear& weapon, const RepairRules& rules);

// Pays the quote with the "repair" sink and restores the weapon to full. All
// or nothing: a refused repair changes neither wallet nor weapon.
[[nodiscard]] RepairQuote repairWeapon(WeaponWear& weapon, const RepairRules& rules, const CurrencyTable& table, Wallet& wallet);

}
