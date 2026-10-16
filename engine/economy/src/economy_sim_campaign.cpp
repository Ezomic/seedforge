#include "ichor/economy/economy_sim_campaign.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"
#include "ichor/economy/access_fees.h"
#include "ichor/economy/rebuild.h"
#include "ichor/factions/standing.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace ichor {

namespace {

constexpr std::string_view kTiersJson = R"([
    {"id": "hostile", "name": "Hostile", "threshold": -1000},
    {"id": "neutral", "name": "Neutral", "threshold": 0},
    {"id": "friendly", "name": "Friendly", "threshold": 250}
])";
constexpr std::string_view kGatesJson = R"([{"id": "safe_passage", "faction": "archonate", "min_tier": "friendly"}])";
constexpr std::string_view kGate = "safe_passage";
constexpr std::string_view kRations = "rations";
constexpr std::string_view kSalvage = "salvage";
constexpr std::uint64_t kMaxRations = 1000000;

bool fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool validRange(std::uint64_t lo, std::uint64_t hi)
{
    return lo <= hi && hi <= kMaxGoodPrice;
}

bool validate(const CurrencyTable& table, const EconomySimTuning& t, std::string& error)
{
    for (const std::string_view source : {"loot", "quest", "trade"}) {
        if (!table.canEarnFrom(CurrencySlot::Coin, source)) {
            return fail(error, "the currency table does not let coin be earned from " + std::string(source));
        }
    }
    for (const std::string_view sink : {"trade", "repair", "dues", "rebuild"}) {
        if (!table.canSpendOn(CurrencySlot::Coin, sink)) {
            return fail(error, "the currency table does not let coin be spent on " + std::string(sink));
        }
    }
    if (t.days == 0 || t.days > kMaxSimDays) {
        return fail(error, "days must be 1 to kMaxSimDays");
    }
    if (t.richThreshold == 0 || t.richThreshold > kMaxGoodPrice) {
        return fail(error, "richThreshold must be 1 to kMaxGoodPrice");
    }
    if (t.richExitPermille > 1000) {
        return fail(error, "richExitPermille must be 0 to 1000");
    }
    if (t.lootChancePermille > 1000 || t.rebuildSharePermille > 1000) {
        return fail(error, "a permille above 1000");
    }
    if (!validRange(t.lootMin, t.lootMax) || !validRange(t.questRewardMin, t.questRewardMax) || !validRange(t.windfallRewardMin, t.windfallRewardMax)) {
        return fail(error, "an income range is inverted or above kMaxGoodPrice");
    }
    if (t.saleIntervalDays == 0 || t.saleUnitsMin > t.saleUnitsMax || t.saleUnitsMax > kMaxRations) {
        return fail(error, "bad salvage sale interval or units");
    }
    if (t.questIntervalMin == 0 || t.questIntervalMin > t.questIntervalMax) {
        return fail(error, "bad quest interval");
    }
    if (t.windfallMin > t.windfallMax || (t.windfallMax > 0 && t.days / t.windfallMax < 4)) {
        return fail(error, "hoards need windfallMin <= windfallMax and a slice of at least 4 days each");
    }
    if (t.rationsPerDay > kMaxRations || t.repairIntervalDays == 0 || t.repairCost > kMaxGoodPrice || t.duesAmount > kMaxGoodPrice) {
        return fail(error, "bad upkeep, repair or dues");
    }
    if (t.duesAmount > 0 && (t.duesPeriodDays == 0 || t.duesPeriodDays > kMaxFeePeriod)) {
        return fail(error, "dues need a period of 1 to kMaxFeePeriod");
    }
    if (t.rebuildReserve > kMaxCurrencyCap) {
        return fail(error, "rebuildReserve above the currency cap");
    }
    if (t.rebuildCosts.size() > kMaxSimRebuildProjects) {
        return fail(error, "too many rebuild projects");
    }
    for (const std::uint64_t cost : t.rebuildCosts) {
        if (cost == 0 || cost > kMaxCurrencyCap) {
            return fail(error, "a rebuild project cost must be 1 to kMaxCurrencyCap");
        }
    }
    return true;
}

std::uint64_t draw(Rng& rng, std::uint64_t lo, std::uint64_t hi)
{
    return lo + rng.range(0, static_cast<std::uint32_t>(hi - lo));
}

std::uint32_t draw32(Rng& rng, std::uint32_t lo, std::uint32_t hi)
{
    return rng.range(lo, hi);
}

std::uint64_t offerValue(const std::vector<MerchantOffer>& offers, std::string_view good, bool buyback)
{
    for (const MerchantOffer& offer : offers) {
        if (offer.good == good) {
            return buyback ? offer.buyback : offer.price;
        }
    }
    return 0;
}

class Campaign {
public:
    Campaign(const CurrencyTable& table, const EconomySimTuning& tuning, std::uint64_t seed)
        : table_(table)
        , t_(tuning)
        , rng_(seed)
        , lootRng_(rng_.derive("loot"))
        , saleRng_(rng_.derive("sale"))
        , hoardRng_(rng_.derive("hoard"))
        , questRng_(rng_.derive("quest"))
    {
    }

    bool setup(std::string& error);
    EconomySimReport run();

private:
    void restock();
    void income(std::uint32_t day);
    void upkeep(std::uint32_t day);
    void sample();
    void invest(std::uint32_t day);
    void earn(std::uint64_t amount, std::string_view source, std::uint64_t& counter);
    void closeSpell();
    void spend(std::uint64_t amount, std::string_view sink, std::uint64_t& counter);

    const CurrencyTable& table_;
    const EconomySimTuning& t_;
    Rng rng_;
    Rng lootRng_;
    Rng saleRng_;
    Rng hoardRng_;
    Rng questRng_;

    Wallet wallet_;
    std::optional<AccessFees> fees_;
    std::optional<FactionStanding> standing_;
    std::optional<RebuildBoard> board_;
    std::vector<std::uint32_t> projects_;
    std::vector<MerchantOffer> offers_;
    RouteStatus route_ = RouteStatus::Cut;
    std::vector<std::uint32_t> hoardDays_;
    std::uint32_t nextQuest_ = 0;

    bool rich_ = false;
    std::uint32_t spell_ = 0;
    EconomySimReport report_;
};

bool Campaign::setup(std::string& error)
{
    const std::vector<Faction> factions = {{"archonate", "The Archonate", FactionRole::Incumbent}};
    std::vector<StandingTier> tiers;
    std::vector<StandingGate> gates;
    std::vector<std::string> problems;
    if (!parseStandingTiers("sim-tiers", kTiersJson, tiers, problems) || !parseStandingGates("sim-gates", kGatesJson, tiers, factions, gates, problems)) {
        return fail(error, "standing setup: " + (problems.empty() ? std::string("failed") : problems.front()));
    }
    standing_.emplace(factions, tiers, gates);

    FactionFee fee;
    fee.faction = "archonate";
    fee.duesAmount = t_.duesAmount;
    fee.duesPeriod = t_.duesPeriodDays;
    fee.duesMinTier = "neutral";
    std::vector<FactionFee> fees;
    if (t_.duesAmount > 0) {
        fees.push_back(fee);
    }
    fees_ = AccessFees::create(std::move(fees), tiers, gates, factions, error);
    if (!fees_) {
        return false;
    }

    std::vector<RebuildSite> sites = {{"supply_road", SiteKind::Route, SiteCondition::Ruined}};
    for (std::size_t i = 1; i < t_.rebuildCosts.size(); ++i) {
        sites.push_back({"site_" + std::to_string(i), SiteKind::Settlement, SiteCondition::Ruined});
    }
    board_ = RebuildBoard::create(sites, error);
    if (!board_) {
        return false;
    }
    for (std::size_t i = 0; i < t_.rebuildCosts.size(); ++i) {
        std::uint32_t id = 0;
        if (board_->start(sites[i].id, SiteCondition::Restored, t_.rebuildCosts[i], id) != StartStatus::Ok) {
            return fail(error, "could not open rebuild project " + std::to_string(i));
        }
        projects_.push_back(id);
    }

    const std::uint32_t hoards = t_.windfallMax == 0 ? 0 : draw32(hoardRng_, t_.windfallMin, t_.windfallMax);
    for (std::uint32_t i = 0; i < hoards; ++i) {
        const std::uint32_t slice = t_.days / hoards;
        hoardDays_.push_back(i * slice + draw32(hoardRng_, slice / 4, slice * 3 / 4));
    }
    nextQuest_ = draw32(questRng_, t_.questIntervalMin, t_.questIntervalMax);

    restock();
    return !offers_.empty();
}

void Campaign::restock()
{
    const std::vector<GoodDef> goods = {{std::string(kRations), 4, 100}, {std::string(kSalvage), 30, 100}};
    RegionMarket market;
    market.scarce = {std::string(kRations)};
    market.abundant = {std::string(kSalvage)};
    market.route = route_;
    std::string error;
    auto stocked = stockMerchant(goods, market, t_.pricing, error);
    offers_ = stocked ? std::move(*stocked) : std::vector<MerchantOffer>{};
}

void Campaign::earn(std::uint64_t amount, std::string_view source, std::uint64_t& counter)
{
    const EarnResult result = wallet_.earn(table_, CurrencySlot::Coin, amount, source);
    if (result.status == TxStatus::Ok) {
        counter += result.credited;
    }
}

void Campaign::spend(std::uint64_t amount, std::string_view sink, std::uint64_t& counter)
{
    if (wallet_.spend(table_, CurrencySlot::Coin, amount, sink) == TxStatus::Ok) {
        counter += amount;
    }
}

void Campaign::income(std::uint32_t day)
{
    if (std::find(hoardDays_.begin(), hoardDays_.end(), day) != hoardDays_.end()) {
        earn(draw(hoardRng_, t_.windfallRewardMin, t_.windfallRewardMax), "loot", report_.earnedHoards);
    }
    if (lootRng_.range(0, 999) < t_.lootChancePermille) {
        earn(draw(lootRng_, t_.lootMin, t_.lootMax), "loot", report_.earnedLoot);
    }
    if (day > 0 && day % t_.saleIntervalDays == 0) {
        const std::uint64_t units = draw32(saleRng_, t_.saleUnitsMin, t_.saleUnitsMax);
        earn(units * offerValue(offers_, kSalvage, true), "trade", report_.earnedTrade);
    }
    if (day == nextQuest_) {
        earn(draw(questRng_, t_.questRewardMin, t_.questRewardMax), "quest", report_.earnedQuest);
        nextQuest_ = day + draw32(questRng_, t_.questIntervalMin, t_.questIntervalMax);
    }
}

void Campaign::upkeep(std::uint32_t day)
{
    const std::uint64_t supplies = offerValue(offers_, kRations, false) * t_.rationsPerDay;
    if (supplies > 0) {
        if (wallet_.canAfford(CurrencySlot::Coin, supplies)) {
            spend(supplies, "trade", report_.spentSupplies);
        } else {
            ++report_.missedUpkeepDays;
        }
    }
    if (day > 0 && day % t_.repairIntervalDays == 0 && t_.repairCost > 0 && wallet_.canAfford(CurrencySlot::Coin, t_.repairCost)) {
        spend(t_.repairCost, "repair", report_.spentRepair);
    }
    if (t_.duesAmount > 0 && !fees_->hasAccess(*standing_, kGate, day)) {
        const std::uint64_t before = wallet_.balance(CurrencySlot::Coin);
        if (fees_->payDues(wallet_, table_, *standing_, "archonate", day) == FeeStatus::Ok) {
            report_.spentDues += before - wallet_.balance(CurrencySlot::Coin);
        }
    }
}

void Campaign::closeSpell()
{
    report_.richPeriodDays.push_back(spell_);
    rich_ = false;
    spell_ = 0;
}

void Campaign::sample()
{
    const std::uint64_t coin = wallet_.balance(CurrencySlot::Coin);
    report_.peakCoin = std::max(report_.peakCoin, coin);
    const std::uint64_t exitAt = t_.richThreshold * t_.richExitPermille / 1000;
    if (rich_ && coin < exitAt) {
        closeSpell();
    }
    if (rich_) {
        ++spell_;
        ++report_.richDays;
    } else if (coin >= t_.richThreshold) {
        rich_ = true;
        spell_ = 1;
        ++report_.richDays;
    }
}

void Campaign::invest(std::uint32_t day)
{
    const std::uint64_t coin = wallet_.balance(CurrencySlot::Coin);
    if (t_.rebuildSharePermille == 0 || coin <= t_.rebuildReserve) {
        return;
    }
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        const RebuildProject* project = board_->project(projects_[i]);
        if (project == nullptr || project->done) {
            continue;
        }
        const std::uint64_t pay = std::max<std::uint64_t>(1, (coin - t_.rebuildReserve) * t_.rebuildSharePermille / 1000);
        const FundResult result = board_->fund(wallet_, table_, projects_[i], pay);
        report_.spentRebuild += result.spent;
        if (result.completed && i == 0) {
            route_ = board_->routeStatus("supply_road").value_or(route_);
            restock();
        }
        break;
    }
    const bool allDone = std::all_of(projects_.begin(), projects_.end(), [&](std::uint32_t id) {
        const RebuildProject* project = board_->project(id);
        return project != nullptr && project->done;
    });
    if (allDone && report_.sinksExhaustedDay == 0) {
        report_.sinksExhaustedDay = day + 1;
    }
}

EconomySimReport Campaign::run()
{
    report_.days = t_.days;
    for (std::uint32_t day = 0; day < t_.days; ++day) {
        income(day);
        upkeep(day);
        sample();
        invest(day);
    }
    if (rich_) {
        closeSpell();
    }
    report_.finalCoin = wallet_.balance(CurrencySlot::Coin);

    std::uint64_t digest = hashCombine(wallet_.digest(), fees_->digest());
    digest = hashCombine(digest, report_.richDays);
    for (const std::uint32_t spell : report_.richPeriodDays) {
        digest = hashCombine(digest, spell);
    }
    digest = hashCombine(digest, report_.peakCoin);
    report_.digest = digest;
    return report_;
}

}

std::optional<EconomySimReport> runEconomySim(const CurrencyTable& table, const EconomySimTuning& tuning, std::uint64_t seed, std::string& error)
{
    error.clear();
    if (!validate(table, tuning, error)) {
        return std::nullopt;
    }
    Campaign campaign(table, tuning, seed);
    if (!campaign.setup(error)) {
        if (error.empty()) {
            error = "the merchant could not be stocked";
        }
        return std::nullopt;
    }
    return campaign.run();
}

bool meetsTarget(const EconomySimReport& report, const EconomyTarget& target)
{
    const auto periods = static_cast<std::uint32_t>(report.richPeriodDays.size());
    if (report.days == 0 || periods < target.minPeriods || periods > target.maxPeriods) {
        return false;
    }
    for (const std::uint32_t spell : report.richPeriodDays) {
        if (spell > target.maxPeriodDays) {
            return false;
        }
    }
    return static_cast<std::uint64_t>(report.richDays) * 1000 <= static_cast<std::uint64_t>(report.days) * target.maxRichPermille;
}

std::optional<EconomySweep> sweepEconomySim(const CurrencyTable& table, const EconomySimTuning& tuning, const EconomyTarget& target,
    std::uint64_t firstSeed, std::uint32_t count, std::string& error)
{
    error.clear();
    if (count == 0) {
        error = "a sweep needs at least one seed";
        return std::nullopt;
    }
    EconomySweep sweep;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto report = runEconomySim(table, tuning, firstSeed + i, error);
        if (!report) {
            return std::nullopt;
        }
        const auto periods = static_cast<std::uint32_t>(report->richPeriodDays.size());
        sweep.fewestPeriods = i == 0 ? periods : std::min(sweep.fewestPeriods, periods);
        sweep.mostPeriods = std::max(sweep.mostPeriods, periods);
        sweep.totalPeriods += periods;
        sweep.totalRichDays += report->richDays;
        sweep.totalDays += report->days;
        bool longSpell = false;
        for (const std::uint32_t spell : report->richPeriodDays) {
            sweep.longestPeriodDays = std::max(sweep.longestPeriodDays, spell);
            longSpell = longSpell || spell > target.maxPeriodDays;
        }
        sweep.seedsWithLongSpell += longSpell ? 1u : 0u;
        sweep.hits += meetsTarget(*report, target) ? 1u : 0u;
        ++sweep.seeds;
    }
    return sweep;
}

}
