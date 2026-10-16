#include "ichor/data/data_set.h"
#include "ichor/economy/law_currency_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
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

std::vector<std::string> shippedLawIds()
{
    std::vector<std::string> ids;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "axioms")) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const auto json = nlohmann::json::parse(in);
        if (json.value("slot", "") == "law") ids.push_back(json.at("id").get<std::string>());
    }
    return ids;
}

Wallet funded(const CurrencyTable& table, std::uint64_t coin, std::uint64_t hard)
{
    Wallet wallet;
    if (coin > 0) REQUIRE(wallet.earn(table, CurrencySlot::Coin, coin, "loot").status == TxStatus::Ok);
    if (hard > 0) REQUIRE(wallet.earn(table, CurrencySlot::Hard, hard, "loot").status == TxStatus::Ok);
    return wallet;
}

const LawSpendContext kPlain{};

}

TEST_CASE("every Law in data/axioms has its own currency row")
{
    const auto ids = shippedLawIds();
    REQUIRE(ids.size() >= 4);
    const auto known = lawCurrencyLawIds();
    for (const std::string& id : ids) {
        INFO(id);
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        CHECK(policy.lawId == id);
        CHECK_FALSE(policy.hardName.empty());
        CHECK_FALSE(policy.refill.empty());
        CHECK_FALSE(policy.spend.empty());
        bool listed = false;
        for (std::string_view k : known) listed = listed || k == id;
        CHECK(listed);
    }
}

TEST_CASE("the rows are well formed and differ from one another")
{
    std::set<std::string_view> names;
    for (std::string_view id : lawCurrencyLawIds()) {
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        REQUIRE(policy.lawId == id);
        names.insert(policy.hardName);
        std::set<std::string_view> seen;
        for (std::string_view word : policy.refill) {
            CHECK_FALSE(word.empty());
            CHECK(seen.insert(word).second);
        }
        seen.clear();
        for (std::string_view word : policy.spend) {
            CHECK_FALSE(word.empty());
            CHECK(seen.insert(word).second);
        }
    }
    CHECK(names.size() == lawCurrencyLawIds().size());
}

TEST_CASE("each Law makes the doc 06 thing scarce or sacred")
{
    const auto& orichalcum = lawCurrencyPolicy("law.orichalcum");
    CHECK(orichalcum.gramsPerCharge > 0);
    CHECK(orichalcum.stealable);
    CHECK(orichalcum.buyableWithCoin);

    const auto& hecatomb = lawCurrencyPolicy("law.hecatomb");
    CHECK_FALSE(hecatomb.buyableWithCoin);
    CHECK(hecatomb.gramsPerCharge == 0);

    const auto& mysteries = lawCurrencyPolicy("law.mysteries");
    CHECK(mysteries.revocable);
    CHECK_FALSE(mysteries.buyableWithCoin);

    const auto& temenos = lawCurrencyPolicy("law.temenos");
    CHECK_FALSE(temenos.portable);

    const auto& favour = lawCurrencyPolicy("law.last_favour");
    CHECK(favour.monthlyPriceRisePermille > 0);
    CHECK_FALSE(favour.buyableWithCoin);

    const auto& oath = lawCurrencyPolicy("law.oath");
    CHECK(oath.debtBacked);
}

TEST_CASE("a missing or unknown Law gets the neutral default")
{
    const std::string nul("law.oath\0x", 10);
    const std::string huge(1 << 20, 'a');
    for (const std::string& id : {std::string(), std::string("law.nope"), std::string("LAW.OATH"), std::string("law.oath "), nul, huge,
                                  std::string("../axioms/oath"), std::string("powers.hegemony")}) {
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        CHECK(policy.lawId.empty());
        CHECK(policy.buyableWithCoin);
        CHECK(policy.portable);
        CHECK_FALSE(policy.revocable);
        CHECK_FALSE(policy.debtBacked);
        CHECK_FALSE(policy.stealable);
        CHECK(policy.gramsPerCharge == 0);
        CHECK(policy.monthlyPriceRisePermille == 0);
    }
}

TEST_CASE("mixed Laws: the first known id wins and unknown ids are skipped")
{
    const std::vector<std::string_view> mixed{"law.nope", "", "law.oath", "law.hecatomb"};
    CHECK(lawCurrencyPolicyFor(mixed).lawId == "law.oath");
    const std::vector<std::string_view> reversed{"law.hecatomb", "law.oath"};
    CHECK(lawCurrencyPolicyFor(reversed).lawId == "law.hecatomb");
    const std::vector<std::string_view> none{"law.nope", "powers.hegemony"};
    CHECK(lawCurrencyPolicyFor(none).lawId.empty());
    CHECK(lawCurrencyPolicyFor({}).lawId.empty());
    const std::vector<std::string_view> twice{"law.oath", "law.oath"};
    CHECK(lawCurrencyPolicyFor(twice).lawId == "law.oath");
}

TEST_CASE("amounts reject negative, NaN, infinite and oversized values")
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
    CHECK_FALSE(lawAmountFromDouble(std::nan("")).has_value());
    CHECK_FALSE(lawAmountFromDouble(kInf).has_value());
    CHECK_FALSE(lawAmountFromDouble(-kInf).has_value());
    CHECK_FALSE(lawAmountFromDouble(-1.0).has_value());
    CHECK_FALSE(lawAmountFromDouble(-0.5).has_value());
    CHECK_FALSE(lawAmountFromDouble(1e300).has_value());
    CHECK_FALSE(lawAmountFromDouble(9007199254740994.0).has_value());
    CHECK(lawAmountFromDouble(0.0) == std::optional<std::uint64_t>(0));
    CHECK(lawAmountFromDouble(-0.0) == std::optional<std::uint64_t>(0));
    CHECK(lawAmountFromDouble(7.9) == std::optional<std::uint64_t>(7));
    CHECK(lawAmountFromDouble(9007199254740992.0) == std::optional<std::uint64_t>(9007199254740992ull));
    CHECK_FALSE(lawAmountFromInt(-1).has_value());
    CHECK_FALSE(lawAmountFromInt(std::numeric_limits<std::int64_t>::min()).has_value());
    CHECK(lawAmountFromInt(std::numeric_limits<std::int64_t>::max()) == std::optional<std::uint64_t>(9223372036854775807ull));
    CHECK(lawAmountFromInt(0) == std::optional<std::uint64_t>(0));
}

TEST_CASE("Last Favour prices climb every month and saturate instead of wrapping")
{
    const auto& favour = lawCurrencyPolicy("law.last_favour");
    const std::uint64_t now = lawPrice(favour, 1000, 0);
    const std::uint64_t later = lawPrice(favour, 1000, 12);
    CHECK(now == 1000);
    CHECK(later > now);
    CHECK(lawPrice(favour, 1000, 24) > later);
    CHECK(lawPrice(favour, 1000, -5) == now);
    CHECK(lawPrice(favour, 1000, std::numeric_limits<std::int64_t>::max()) == lawPrice(favour, 1000, kMaxPriceMonths));
    CHECK(lawPrice(favour, 1000, std::numeric_limits<std::int64_t>::min()) == now);
    CHECK(lawPrice(favour, kMax, 5) == kMax);
    CHECK(lawPrice(favour, kMax / 2, 1) > kMax / 2);
    CHECK(lawPrice(favour, 0, 100) == 0);
    CHECK(lawPrice(lawCurrencyPolicy("law.oath"), 1000, 600) == 1000);
    CHECK(lawPrice(lawCurrencyPolicy(""), kMax, 1200) == kMax);
}

TEST_CASE("Orichalcum charge has weight that saturates")
{
    const auto& orichalcum = lawCurrencyPolicy("law.orichalcum");
    CHECK(lawCarryWeightGrams(orichalcum, 0) == 0);
    CHECK(lawCarryWeightGrams(orichalcum, 10) == 10ull * orichalcum.gramsPerCharge);
    CHECK(lawCarryWeightGrams(orichalcum, kMax) == kMax);
    CHECK(lawCarryWeightGrams(lawCurrencyPolicy("law.hecatomb"), kMax) == 0);
}

TEST_CASE("refilling the hard slot follows the Law's own sources")
{
    const CurrencyTable table = shipped();
    for (std::string_view id : lawCurrencyLawIds()) {
        INFO(id);
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        Wallet wallet;
        for (std::string_view source : policy.refill) {
            const EarnResult result = lawEarnHard(policy, table, wallet, 5, source);
            CHECK(result.status == TxStatus::Ok);
            CHECK(result.credited == 5);
        }
        const EarnResult refused = lawEarnHard(policy, table, wallet, 5, "not_a_source");
        CHECK(refused.status == TxStatus::NotAllowed);
        CHECK(refused.credited == 0);
        CHECK(wallet.balance(CurrencySlot::Hard) == 5 * policy.refill.size());
    }
}

TEST_CASE("Hecatomb vitality refills only by sacrifice, never by loot")
{
    const CurrencyTable table = shipped();
    const auto& hecatomb = lawCurrencyPolicy("law.hecatomb");
    Wallet wallet;
    CHECK(lawEarnHard(hecatomb, table, wallet, 5, "loot").status == TxStatus::NotAllowed);
    CHECK(lawEarnHard(hecatomb, table, wallet, 5, "sacrifice").status == TxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Hard) == 5);
}

TEST_CASE("the cap clips a refill and is never passed or wrapped")
{
    const CurrencyTable table = shipped();
    const auto& orichalcum = lawCurrencyPolicy("law.orichalcum");
    Wallet wallet;
    const EarnResult huge = lawEarnHard(orichalcum, table, wallet, kMax, orichalcum.refill.front());
    CHECK(huge.status == TxStatus::Ok);
    CHECK(huge.credited == table.def(CurrencySlot::Hard).cap);
    CHECK(wallet.balance(CurrencySlot::Hard) == table.def(CurrencySlot::Hard).cap);
    const EarnResult more = lawEarnHard(orichalcum, table, wallet, kMax, orichalcum.refill.front());
    CHECK(more.credited == 0);
    CHECK(wallet.balance(CurrencySlot::Hard) == table.def(CurrencySlot::Hard).cap);
}

TEST_CASE("spending follows the Law's sinks and is all or nothing")
{
    const CurrencyTable table = shipped();
    for (std::string_view id : lawCurrencyLawIds()) {
        INFO(id);
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        const LawSpendContext context{.onConsecratedGround = true, .standingRevoked = false};
        Wallet wallet = funded(table, 0, 10);
        const Wallet before = wallet;
        CHECK(lawSpendHard(policy, table, wallet, 4, "not_a_sink", context) == LawTxStatus::NotAllowed);
        CHECK(wallet == before);
        CHECK(lawSpendHard(policy, table, wallet, 11, policy.spend.front(), context) == LawTxStatus::Insufficient);
        CHECK(wallet == before);
        CHECK(lawSpendHard(policy, table, wallet, 4, policy.spend.front(), context) == LawTxStatus::Ok);
        CHECK(wallet.balance(CurrencySlot::Hard) == 6);
        CHECK(lawSpendHard(policy, table, wallet, kMax, policy.spend.front(), context) == LawTxStatus::Insufficient);
        CHECK(wallet.balance(CurrencySlot::Hard) == 6);
    }
}

TEST_CASE("Temenos charge is spent only on consecrated ground")
{
    const CurrencyTable table = shipped();
    const auto& temenos = lawCurrencyPolicy("law.temenos");
    Wallet wallet = funded(table, 0, 10);
    const Wallet before = wallet;
    CHECK(lawSpendHard(temenos, table, wallet, 3, temenos.spend.front(), kPlain) == LawTxStatus::WrongGround);
    CHECK(wallet == before);
    CHECK(lawSpendHard(temenos, table, wallet, 3, temenos.spend.front(), {.onConsecratedGround = true}) == LawTxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Hard) == 7);
    Wallet other = funded(table, 0, 10);
    CHECK(lawSpendHard(lawCurrencyPolicy("law.orichalcum"), table, other, 3, "power", kPlain) == LawTxStatus::Ok);
}

TEST_CASE("a revoked Mysteries standing locks the tokens, other Laws ignore it")
{
    const CurrencyTable table = shipped();
    const auto& mysteries = lawCurrencyPolicy("law.mysteries");
    Wallet wallet = funded(table, 0, 10);
    const Wallet before = wallet;
    CHECK(lawSpendHard(mysteries, table, wallet, 1, mysteries.spend.front(), {.standingRevoked = true}) == LawTxStatus::Revoked);
    CHECK(wallet == before);
    CHECK(lawSpendHard(mysteries, table, wallet, 1, mysteries.spend.front(), kPlain) == LawTxStatus::Ok);
    Wallet other = funded(table, 0, 10);
    CHECK(lawSpendHard(lawCurrencyPolicy("law.orichalcum"), table, other, 1, "power", {.standingRevoked = true}) == LawTxStatus::Ok);
}

TEST_CASE("the neutral default spends like the plain wallet")
{
    const CurrencyTable table = shipped();
    const auto& neutral = lawCurrencyPolicy("");
    Wallet wallet = funded(table, 0, 10);
    CHECK(lawSpendHard(neutral, table, wallet, 4, "power", kPlain) == LawTxStatus::Ok);
    CHECK(lawSpendHard(neutral, table, wallet, 1, "toll", kPlain) == LawTxStatus::NotAllowed);
    CHECK(lawEarnHard(neutral, table, wallet, 3, "quest").status == TxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Hard) == 9);
}

TEST_CASE("coin buys hard currency only where the Law allows it")
{
    const CurrencyTable table = shipped();
    const auto& orichalcum = lawCurrencyPolicy("law.orichalcum");
    Wallet wallet = funded(table, 100, 0);
    CHECK(lawBuyHard(orichalcum, table, wallet, 40, 5) == LawTxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Coin) == 60);
    CHECK(wallet.balance(CurrencySlot::Hard) == 5);

    for (std::string_view id : {"law.hecatomb", "law.mysteries", "law.last_favour"}) {
        INFO(id);
        Wallet w = funded(table, 100, 0);
        const Wallet before = w;
        CHECK(lawBuyHard(lawCurrencyPolicy(id), table, w, 1, 1) == LawTxStatus::NotAllowed);
        CHECK(w == before);
    }
}

TEST_CASE("buying refuses without enough coin or room under the cap and changes nothing")
{
    const CurrencyTable table = shipped();
    const auto& orichalcum = lawCurrencyPolicy("law.orichalcum");
    Wallet poor = funded(table, 10, 0);
    const Wallet poorBefore = poor;
    CHECK(lawBuyHard(orichalcum, table, poor, 11, 1) == LawTxStatus::Insufficient);
    CHECK(lawBuyHard(orichalcum, table, poor, kMax, 1) == LawTxStatus::Insufficient);
    CHECK(poor == poorBefore);

    const std::uint64_t cap = table.def(CurrencySlot::Hard).cap;
    Wallet full = funded(table, 100, cap);
    const Wallet fullBefore = full;
    CHECK(lawBuyHard(orichalcum, table, full, 1, 1) == LawTxStatus::CapReached);
    CHECK(lawBuyHard(orichalcum, table, full, 1, kMax) == LawTxStatus::CapReached);
    CHECK(full == fullBefore);

    Wallet rich = funded(table, 100, 5);
    CHECK(lawBuyHard(orichalcum, table, rich, 0, 0) == LawTxStatus::Ok);
    CHECK(rich.balance(CurrencySlot::Hard) == 5);
}

TEST_CASE("Oath spends into debt up to the ceiling and keeps it atomic")
{
    const CurrencyTable table = shipped();
    const auto& oath = lawCurrencyPolicy("law.oath");
    Wallet wallet = funded(table, 0, 10);
    LawDebt debt;
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 4, "power", 20) == LawTxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Hard) == 6);
    CHECK(debt.owed == 0);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 16, "power", 20) == LawTxStatus::Ok);
    CHECK(wallet.balance(CurrencySlot::Hard) == 0);
    CHECK(debt.owed == 10);

    const Wallet walletBefore = wallet;
    const LawDebt debtBefore = debt;
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 11, "power", 20) == LawTxStatus::OverCeiling);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, kMax, "power", 20) == LawTxStatus::OverCeiling);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 1, "not_a_sink", 20) == LawTxStatus::NotAllowed);
    CHECK(wallet == walletBefore);
    CHECK(debt == debtBefore);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 10, "power", 20) == LawTxStatus::Ok);
    CHECK(debt.owed == 20);
}

TEST_CASE("debt arithmetic cannot overflow")
{
    const CurrencyTable table = shipped();
    const auto& oath = lawCurrencyPolicy("law.oath");
    Wallet wallet;
    LawDebt debt{kMax - 1};
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 5, "power", kMax) == LawTxStatus::OverCeiling);
    CHECK(debt.owed == kMax - 1);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 1, "power", kMax) == LawTxStatus::Ok);
    CHECK(debt.owed == kMax);
    CHECK(lawSpendOnCredit(oath, table, wallet, debt, 1, "power", kMax) == LawTxStatus::OverCeiling);
}

TEST_CASE("only debt-backed Laws can spend on credit")
{
    const CurrencyTable table = shipped();
    for (std::string_view id : lawCurrencyLawIds()) {
        if (lawCurrencyPolicy(id).debtBacked) continue;
        INFO(id);
        Wallet wallet;
        LawDebt debt;
        const LawCurrencyPolicy& policy = lawCurrencyPolicy(id);
        CHECK(lawSpendOnCredit(policy, table, wallet, debt, 1, policy.spend.front(), 100) == LawTxStatus::NotAllowed);
        CHECK(debt.owed == 0);
    }
    Wallet wallet;
    LawDebt debt;
    CHECK(lawSpendOnCredit(lawCurrencyPolicy(""), table, wallet, debt, 1, "power", 100) == LawTxStatus::NotAllowed);
}

TEST_CASE("Oath debt can be handed to someone else within their ceiling")
{
    const auto& oath = lawCurrencyPolicy("law.oath");
    LawDebt from{30};
    LawDebt to{5};
    CHECK(lawTransferDebt(oath, from, to, 10, 20) == LawTxStatus::Ok);
    CHECK(from.owed == 20);
    CHECK(to.owed == 15);

    const LawDebt fromBefore = from;
    const LawDebt toBefore = to;
    CHECK(lawTransferDebt(oath, from, to, 6, 20) == LawTxStatus::OverCeiling);
    CHECK(lawTransferDebt(oath, from, to, 21, 100) == LawTxStatus::Insufficient);
    CHECK(lawTransferDebt(oath, from, to, kMax, kMax) == LawTxStatus::Insufficient);
    CHECK(from == fromBefore);
    CHECK(to == toBefore);

    LawDebt big{kMax};
    LawDebt receiver{kMax - 1};
    CHECK(lawTransferDebt(oath, big, receiver, 2, kMax) == LawTxStatus::OverCeiling);
    CHECK(receiver.owed == kMax - 1);
    CHECK(lawTransferDebt(oath, big, receiver, 1, kMax) == LawTxStatus::Ok);
    CHECK(receiver.owed == kMax);
    CHECK(big.owed == kMax - 1);
}

TEST_CASE("debt is not transferable under any other Law")
{
    for (std::string_view id : lawCurrencyLawIds()) {
        if (lawCurrencyPolicy(id).debtBacked) continue;
        LawDebt from{10};
        LawDebt to;
        CHECK(lawTransferDebt(lawCurrencyPolicy(id), from, to, 1, 100) == LawTxStatus::NotAllowed);
        CHECK(from.owed == 10);
        CHECK(to.owed == 0);
    }
    LawDebt from{10};
    LawDebt to;
    CHECK(lawTransferDebt(lawCurrencyPolicy(""), from, to, 1, 100) == LawTxStatus::NotAllowed);
}

TEST_CASE("a zero amount is a permitted no-op and still needs a permitted word")
{
    const CurrencyTable table = shipped();
    const auto& oath = lawCurrencyPolicy("law.oath");
    Wallet wallet = funded(table, 0, 3);
    const Wallet before = wallet;
    CHECK(lawSpendHard(oath, table, wallet, 0, oath.spend.front(), kPlain) == LawTxStatus::Ok);
    CHECK(lawSpendHard(oath, table, wallet, 0, "bogus", kPlain) == LawTxStatus::NotAllowed);
    CHECK(lawEarnHard(oath, table, wallet, 0, oath.refill.front()).credited == 0);
    CHECK(wallet == before);
}

TEST_CASE("hostile source and sink ids never match")
{
    const CurrencyTable table = shipped();
    const auto& oath = lawCurrencyPolicy("law.oath");
    const std::string nul("power\0", 6);
    const std::string huge(1 << 20, 'p');
    for (const std::string& word : {std::string(), nul, huge, std::string("POWER"), std::string(" power"), std::string("power\n")}) {
        Wallet wallet = funded(table, 0, 5);
        CHECK(lawSpendHard(oath, table, wallet, 1, word, kPlain) == LawTxStatus::NotAllowed);
        CHECK(lawEarnHard(oath, table, wallet, 1, word).status == TxStatus::NotAllowed);
        CHECK(wallet.balance(CurrencySlot::Hard) == 5);
    }
}
