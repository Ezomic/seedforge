#pragma once

#include "ichor/economy/wallet.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ichor {

// Doc 06 "Two currencies": the Law slot decides what the hard currency is and how it behaves. Each
// Law is one row of constant data (what refills it, what spends it, whether coin can buy it, whether
// it can be carried, revoked, or owed) and the functions below are pure logic on a Wallet, so the
// simulation agrees on every platform. The Wallet and CurrencyTable are used as they are; nothing here
// is wired into the app. The refill and spend words below are the Law's own vocabulary; the Wallet
// still counts every credit against the earn list and every debit against the spend list of
// data/currency/currencies.json, whose words are fixed.

struct LawCurrencyPolicy {
    std::string_view lawId;   // "law.orichalcum"; empty for the neutral default
    std::string_view hardName; // what the hard currency is in this Law
    std::span<const std::string_view> refill; // sources that may credit it
    std::span<const std::string_view> spend;  // sinks that may debit it
    bool buyableWithCoin = true;              // false: coin can never buy it
    bool portable = true;                     // false: spent only on consecrated ground
    bool revocable = false;                   // true: a revoked standing locks it
    bool debtBacked = false;                  // true: a spend may overdraw into transferable debt
    bool stealable = false;                   // true: a thief can take it from the holder
    std::uint32_t gramsPerCharge = 0;         // weight of one charge; 0 is weightless
    std::uint32_t monthlyPriceRisePermille = 0; // deflation: prices climb this much per month
};

// Months beyond this change nothing, so the price arithmetic has a fixed bound.
constexpr std::int64_t kMaxPriceMonths = 1200;

// The row for an exact Law id ("law.hecatomb"). Anything else (empty, unknown, different case,
// embedded NUL, a very long string) gets the neutral default, which behaves like the plain wallet.
const LawCurrencyPolicy& lawCurrencyPolicy(std::string_view lawId);

// A seed has one Law, but a caller may hold several candidate ids. The first one that names a known
// Law wins; unknown ids are skipped; if none does the neutral default is returned.
const LawCurrencyPolicy& lawCurrencyPolicyFor(std::span<const std::string_view> lawIds);

// The ids of every Law that has its own row, in a fixed order.
std::span<const std::string_view> lawCurrencyLawIds();

// Nothing for a negative, NaN, infinite or larger-than-2^53 amount; otherwise the amount rounded down.
std::optional<std::uint64_t> lawAmountFromDouble(double amount);
std::optional<std::uint64_t> lawAmountFromInt(std::int64_t amount);

// base * (1000 + monthlyPriceRisePermille * months) / 1000, rounded down and saturating at the
// uint64 maximum. Months below 0 count as 0 and above kMaxPriceMonths as kMaxPriceMonths.
std::uint64_t lawPrice(const LawCurrencyPolicy& policy, std::uint64_t base, std::int64_t months);

// Weight of a balance in grams, saturating.
std::uint64_t lawCarryWeightGrams(const LawCurrencyPolicy& policy, std::uint64_t charges);

enum class LawTxStatus : std::uint8_t { Ok, NotAllowed, Insufficient, WrongGround, Revoked, OverCeiling, CapReached };

struct LawSpendContext {
    bool onConsecratedGround = false;
    bool standingRevoked = false;
};

// Credits the hard slot if the Law lets `source` refill it. The cap clips as in Wallet::earn.
[[nodiscard]] EarnResult lawEarnHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet,
                                     std::uint64_t amount, std::string_view source);

// Debits the hard slot if the Law lets `sink` spend it and the context allows it. All or nothing.
[[nodiscard]] LawTxStatus lawSpendHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet,
                                       std::uint64_t amount, std::string_view sink, const LawSpendContext& context);

// Coin buys hard currency where the Law allows it: debits `coinCost` coin for "trade" and credits
// `hardAmount`. All or nothing; CapReached when the credit would not fit under the cap.
[[nodiscard]] LawTxStatus lawBuyHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet,
                                     std::uint64_t coinCost, std::uint64_t hardAmount);

// The Oath: debt owed in hard currency. It can move between holders.
struct LawDebt {
    std::uint64_t owed = 0;
    friend bool operator==(const LawDebt&, const LawDebt&) = default;
};

// Spends from the balance first and owes the shortfall, up to `ceiling` total debt. All or nothing.
// NotAllowed when the Law is not debt backed or the sink is not a spend sink of the Law.
[[nodiscard]] LawTxStatus lawSpendOnCredit(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet,
                                           LawDebt& debt, std::uint64_t amount, std::string_view sink, std::uint64_t ceiling);

// Moves `amount` of obligation from one holder to another, up to the receiver's ceiling. All or nothing.
[[nodiscard]] LawTxStatus lawTransferDebt(const LawCurrencyPolicy& policy, LawDebt& from, LawDebt& to, std::uint64_t amount,
                                          std::uint64_t receiverCeiling);

}
