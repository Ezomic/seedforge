#pragma once

#include "ichor/data/data_set.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06: coin buys bread, charge buys power. The two slots are fixed; what
// the hard slot is (its name, display and rules) comes from data, so swapping
// the hard currency is a data edit and not a second economy implementation.
enum class CurrencySlot : std::uint8_t { Coin, Hard };

constexpr std::size_t kCurrencySlotCount = 2;

// Largest cap data may name: a JSON number is a double, exact up to 2^53.
constexpr std::uint64_t kMaxCurrencyCap = 9007199254740992ull;

struct CurrencyDef {
    std::string id; // "coin" or "hard"
    std::string name;
    std::string symbol;
    std::vector<std::string> earn;  // sources that may credit this currency
    std::vector<std::string> spend; // sinks that may debit it
    std::uint64_t cap = 0;          // the most a wallet can hold
};

// The validated currency table (data/currency/*.json): exactly one "coin" and
// one "hard" record, each with a non-empty name and symbol, earn and spend
// lists of distinct non-empty strings, and an integer cap of 1 to kMaxCurrencyCap.
class CurrencyTable {
public:
    static std::optional<CurrencyTable> fromTable(const DataTable& table, std::string& error);

    const CurrencyDef& def(CurrencySlot slot) const { return defs_[static_cast<std::size_t>(slot)]; }
    bool canEarnFrom(CurrencySlot slot, std::string_view source) const;
    bool canSpendOn(CurrencySlot slot, std::string_view sink) const;

private:
    CurrencyTable() = default;

    std::array<CurrencyDef, kCurrencySlotCount> defs_;
};

enum class TxStatus { Ok, NotAllowed, Insufficient };

struct EarnResult {
    TxStatus status = TxStatus::Ok;
    std::uint64_t credited = 0; // less than asked when the cap clipped it
};

// Unsigned balances, so none can go negative, and every update is checked
// against the balance or the cap first, so none can wrap. A zero amount is an
// allowed no-op (a free item costs nothing) but still needs a permitted
// source or sink.
class Wallet {
public:
    std::uint64_t balance(CurrencySlot slot) const { return balances_[static_cast<std::size_t>(slot)]; }
    bool canAfford(CurrencySlot slot, std::uint64_t amount) const { return balance(slot) >= amount; }

    // Credits up to the cap; the surplus is lost and `credited` says how much landed.
    EarnResult earn(const CurrencyTable& table, CurrencySlot slot, std::uint64_t amount, std::string_view source);
    // All or nothing: a refused spend changes no balance.
    TxStatus spend(const CurrencyTable& table, CurrencySlot slot, std::uint64_t amount, std::string_view sink);

    // Little-endian bytes: a format version, then the balances in slot order.
    std::vector<std::uint8_t> serialise() const;
    // Nothing for truncated, trailing, wrong-version or over-cap bytes.
    static std::optional<Wallet> deserialise(std::span<const std::uint8_t> bytes, const CurrencyTable& table);

    // FNV-1a of serialise(): identical on every platform.
    std::uint64_t digest() const;

    friend bool operator==(const Wallet&, const Wallet&) = default;

private:
    std::array<std::uint64_t, kCurrencySlotCount> balances_{};
};

}
