#include "ichor/economy/law_currency_policy.h"

#include <array>
#include <cmath>
#include <limits>

namespace ichor {

namespace {

constexpr std::uint64_t kMaxU64 = std::numeric_limits<std::uint64_t>::max();

constexpr std::array<std::string_view, 3> kOrichalcumRefill{"loot", "quest", "smelt"};
constexpr std::array<std::string_view, 2> kOrichalcumSpend{"power", "trade"};
constexpr std::array<std::string_view, 1> kHecatombRefill{"sacrifice"};
constexpr std::array<std::string_view, 2> kHecatombSpend{"power", "rite"};
constexpr std::array<std::string_view, 1> kMysteriesRefill{"initiation"};
constexpr std::array<std::string_view, 1> kMysteriesSpend{"rite"};
constexpr std::array<std::string_view, 1> kTemenosRefill{"consecration"};
constexpr std::array<std::string_view, 2> kTemenosSpend{"rite", "tithe"};
constexpr std::array<std::string_view, 1> kFavourRefill{"loot"};
constexpr std::array<std::string_view, 1> kFavourSpend{"power"};
constexpr std::array<std::string_view, 1> kOathRefill{"pledge"};
constexpr std::array<std::string_view, 2> kOathSpend{"power", "trade"};
constexpr std::array<std::string_view, 2> kNeutralRefill{"loot", "quest"};
constexpr std::array<std::string_view, 2> kNeutralSpend{"power", "trade"};

constexpr std::array<std::string_view, 6> kLawIds{"law.orichalcum", "law.hecatomb", "law.mysteries", "law.temenos", "law.last_favour", "law.oath"};

const LawCurrencyPolicy kPolicies[] = {
    {.lawId = "law.orichalcum", .hardName = "refined ore charges", .refill = kOrichalcumRefill, .spend = kOrichalcumSpend,
     .buyableWithCoin = true, .portable = true, .revocable = false, .debtBacked = false, .stealable = true,
     .gramsPerCharge = 500, .monthlyPriceRisePermille = 0},
    {.lawId = "law.hecatomb", .hardName = "bottled vitality", .refill = kHecatombRefill, .spend = kHecatombSpend,
     .buyableWithCoin = false, .portable = true, .revocable = false, .debtBacked = false, .stealable = false,
     .gramsPerCharge = 0, .monthlyPriceRisePermille = 0},
    {.lawId = "law.mysteries", .hardName = "tokens of initiation", .refill = kMysteriesRefill, .spend = kMysteriesSpend,
     .buyableWithCoin = false, .portable = true, .revocable = true, .debtBacked = false, .stealable = false,
     .gramsPerCharge = 0, .monthlyPriceRisePermille = 0},
    {.lawId = "law.temenos", .hardName = "sanctuary claims", .refill = kTemenosRefill, .spend = kTemenosSpend,
     .buyableWithCoin = true, .portable = false, .revocable = false, .debtBacked = false, .stealable = false,
     .gramsPerCharge = 0, .monthlyPriceRisePermille = 0},
    {.lawId = "law.last_favour", .hardName = "remaining charges", .refill = kFavourRefill, .spend = kFavourSpend,
     .buyableWithCoin = false, .portable = true, .revocable = false, .debtBacked = false, .stealable = false,
     .gramsPerCharge = 0, .monthlyPriceRisePermille = 50},
    {.lawId = "law.oath", .hardName = "transferable debt", .refill = kOathRefill, .spend = kOathSpend,
     .buyableWithCoin = true, .portable = true, .revocable = false, .debtBacked = true, .stealable = false,
     .gramsPerCharge = 0, .monthlyPriceRisePermille = 0},
};

const LawCurrencyPolicy kNeutral{.lawId = {}, .hardName = "charge", .refill = kNeutralRefill, .spend = kNeutralSpend};

bool listed(std::span<const std::string_view> words, std::string_view word)
{
    for (std::string_view w : words) {
        if (w == word) return true;
    }
    return false;
}

// The word the Wallet counts a hard credit or debit under: the first one data/currency names.
std::string_view tableWord(const std::vector<std::string>& words)
{
    return words.empty() ? std::string_view() : std::string_view(words.front());
}

}

const LawCurrencyPolicy& lawCurrencyPolicy(std::string_view lawId)
{
    for (const LawCurrencyPolicy& policy : kPolicies) {
        if (policy.lawId == lawId) return policy;
    }
    return kNeutral;
}

const LawCurrencyPolicy& lawCurrencyPolicyFor(std::span<const std::string_view> lawIds)
{
    for (std::string_view id : lawIds) {
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        if (!policy.lawId.empty()) return policy;
    }
    return kNeutral;
}

std::span<const std::string_view> lawCurrencyLawIds()
{
    return kLawIds;
}

std::optional<std::uint64_t> lawAmountFromDouble(double amount)
{
    if (std::isnan(amount) || amount < 0.0 || amount > static_cast<double>(kMaxCurrencyCap)) return std::nullopt;
    return static_cast<std::uint64_t>(amount);
}

std::optional<std::uint64_t> lawAmountFromInt(std::int64_t amount)
{
    if (amount < 0) return std::nullopt;
    return static_cast<std::uint64_t>(amount);
}

std::uint64_t lawPrice(const LawCurrencyPolicy& policy, std::uint64_t base, std::int64_t months)
{
    const std::uint64_t m = static_cast<std::uint64_t>(months < 0 ? 0 : (months > kMaxPriceMonths ? kMaxPriceMonths : months));
    const std::uint64_t factor = 1000 + static_cast<std::uint64_t>(policy.monthlyPriceRisePermille) * m;
    const std::uint64_t whole = base / 1000;
    if (whole > kMaxU64 / factor) return kMaxU64;
    const std::uint64_t high = whole * factor;
    const std::uint64_t low = (base % 1000) * factor / 1000;
    return high > kMaxU64 - low ? kMaxU64 : high + low;
}

std::uint64_t lawCarryWeightGrams(const LawCurrencyPolicy& policy, std::uint64_t charges)
{
    if (policy.gramsPerCharge == 0) return 0;
    return charges > kMaxU64 / policy.gramsPerCharge ? kMaxU64 : charges * policy.gramsPerCharge;
}

EarnResult lawEarnHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet, std::uint64_t amount,
                       std::string_view source)
{
    const std::string_view word = tableWord(table.def(CurrencySlot::Hard).earn);
    if (word.empty() || !listed(policy.refill, source)) return EarnResult{TxStatus::NotAllowed, 0};
    return wallet.earn(table, CurrencySlot::Hard, amount, word);
}

LawTxStatus lawSpendHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet, std::uint64_t amount,
                         std::string_view sink, const LawSpendContext& context)
{
    const std::string_view word = tableWord(table.def(CurrencySlot::Hard).spend);
    if (word.empty() || !listed(policy.spend, sink)) return LawTxStatus::NotAllowed;
    if (policy.revocable && context.standingRevoked) return LawTxStatus::Revoked;
    if (!policy.portable && !context.onConsecratedGround) return LawTxStatus::WrongGround;
    return wallet.spend(table, CurrencySlot::Hard, amount, word) == TxStatus::Ok ? LawTxStatus::Ok : LawTxStatus::Insufficient;
}

LawTxStatus lawBuyHard(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet, std::uint64_t coinCost,
                       std::uint64_t hardAmount)
{
    const std::string_view earnWord = tableWord(table.def(CurrencySlot::Hard).earn);
    if (!policy.buyableWithCoin || earnWord.empty()) return LawTxStatus::NotAllowed;
    Wallet trial = wallet;
    switch (trial.spend(table, CurrencySlot::Coin, coinCost, "trade")) {
    case TxStatus::Ok: break;
    case TxStatus::Insufficient: return LawTxStatus::Insufficient;
    case TxStatus::NotAllowed: return LawTxStatus::NotAllowed;
    }
    const EarnResult credit = trial.earn(table, CurrencySlot::Hard, hardAmount, earnWord);
    if (credit.status != TxStatus::Ok) return LawTxStatus::NotAllowed;
    if (credit.credited != hardAmount) return LawTxStatus::CapReached;
    wallet = trial;
    return LawTxStatus::Ok;
}

LawTxStatus lawSpendOnCredit(const LawCurrencyPolicy& policy, const CurrencyTable& table, Wallet& wallet, LawDebt& debt,
                             std::uint64_t amount, std::string_view sink, std::uint64_t ceiling)
{
    const std::string_view word = tableWord(table.def(CurrencySlot::Hard).spend);
    if (!policy.debtBacked || word.empty() || !listed(policy.spend, sink)) return LawTxStatus::NotAllowed;
    const std::uint64_t held = wallet.balance(CurrencySlot::Hard);
    const std::uint64_t fromBalance = amount < held ? amount : held;
    const std::uint64_t shortfall = amount - fromBalance;
    if (shortfall > 0) {
        if (debt.owed > ceiling || shortfall > ceiling - debt.owed) return LawTxStatus::OverCeiling;
    }
    if (wallet.spend(table, CurrencySlot::Hard, fromBalance, word) != TxStatus::Ok) return LawTxStatus::NotAllowed;
    debt.owed += shortfall;
    return LawTxStatus::Ok;
}

LawTxStatus lawTransferDebt(const LawCurrencyPolicy& policy, LawDebt& from, LawDebt& to, std::uint64_t amount,
                            std::uint64_t receiverCeiling)
{
    if (!policy.debtBacked) return LawTxStatus::NotAllowed;
    if (amount > from.owed) return LawTxStatus::Insufficient;
    if (to.owed > receiverCeiling || amount > receiverCeiling - to.owed) return LawTxStatus::OverCeiling;
    from.owed -= amount;
    to.owed += amount;
    return LawTxStatus::Ok;
}

}
