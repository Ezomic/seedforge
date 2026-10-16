#include "ichor/data/data_set.h"
#include "ichor/economy/economy_sim_campaign.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

constexpr std::uint32_t kSeeds = 300;

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

EconomySimReport run(const CurrencyTable& table, const EconomySimTuning& tuning, std::uint64_t seed)
{
    std::string error;
    auto report = runEconomySim(table, tuning, seed, error);
    INFO(error);
    REQUIRE(report.has_value());
    return *report;
}

EconomySweep sweep(const CurrencyTable& table, const EconomySimTuning& tuning)
{
    std::string error;
    auto result = sweepEconomySim(table, tuning, EconomyTarget{}, 1, kSeeds, error);
    INFO(error);
    REQUIRE(result.has_value());
    return *result;
}

bool rejects(const CurrencyTable& table, const EconomySimTuning& tuning)
{
    std::string error;
    const bool refused = !runEconomySim(table, tuning, 1, error).has_value();
    CHECK((refused == !error.empty()));
    return refused;
}

EconomySimReport reportOf(std::uint32_t days, std::vector<std::uint32_t> spells)
{
    EconomySimReport report;
    report.days = days;
    report.richPeriodDays = std::move(spells);
    for (const std::uint32_t spell : report.richPeriodDays) {
        report.richDays += spell;
    }
    return report;
}

}

TEST_CASE("the current tuning makes the player briefly rich two or three times per campaign")
{
    const CurrencyTable table = shipped();
    const EconomySweep result = sweep(table, EconomySimTuning{});

    INFO("hits " << result.hits << "/" << result.seeds << ", periods " << result.fewestPeriods << ".." << result.mostPeriods
                 << ", longest spell " << result.longestPeriodDays << " days, rich days " << result.totalRichDays << "/" << result.totalDays);
    CHECK(result.hits == result.seeds);
    CHECK(result.fewestPeriods >= 2);
    CHECK(result.mostPeriods <= 3);
    CHECK(result.longestPeriodDays <= EconomyTarget{}.maxPeriodDays);
    CHECK(result.totalRichDays * 1000 <= result.totalDays * EconomyTarget{}.maxRichPermille);
    // A spell that never forms would also be "brief": there must be real riches.
    CHECK(result.totalRichDays >= result.seeds * 2);
}

TEST_CASE("without any sink the player is rich for the rest of the campaign")
{
    const CurrencyTable table = shipped();
    EconomySimTuning tuning;
    tuning.rebuildSharePermille = 0;
    tuning.duesAmount = 0;
    tuning.repairCost = 0;
    tuning.rationsPerDay = 0;

    const EconomySweep result = sweep(table, tuning);
    CHECK(result.hits == 0);
    CHECK(result.seedsWithLongSpell == result.seeds);
    CHECK(result.totalRichDays * 1000 > result.totalDays * 400);
}

TEST_CASE("when hoards never reach the threshold the player is never rich")
{
    const CurrencyTable table = shipped();
    EconomySimTuning tuning;
    tuning.windfallRewardMin = 100;
    tuning.windfallRewardMax = 200;

    const EconomySweep result = sweep(table, tuning);
    CHECK(result.hits == 0);
    CHECK(result.mostPeriods == 0);
    CHECK(result.totalRichDays == 0);
}

TEST_CASE("a threshold the wallet always clears is always rich")
{
    const CurrencyTable table = shipped();
    EconomySimTuning tuning;
    tuning.richThreshold = 1;
    tuning.rebuildReserve = 100000;

    const EconomySweep result = sweep(table, tuning);
    CHECK(result.hits == 0);
    CHECK(result.seedsWithLongSpell > 0);
}

TEST_CASE("restoring the cut trade route lowers what supplies cost")
{
    const CurrencyTable table = shipped();
    EconomySimTuning funded;
    EconomySimTuning stranded;
    stranded.rebuildSharePermille = 0;

    std::uint64_t fundedSpend = 0;
    std::uint64_t strandedSpend = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        fundedSpend += run(table, funded, seed).spentSupplies;
        strandedSpend += run(table, stranded, seed).spentSupplies;
    }
    CHECK(fundedSpend < strandedSpend);
}

TEST_CASE("the coin ledger balances: earned minus spent is what is left")
{
    const CurrencyTable table = shipped();
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        const EconomySimReport r = run(table, EconomySimTuning{}, seed);
        const std::uint64_t earned = r.earnedLoot + r.earnedHoards + r.earnedQuest + r.earnedTrade;
        const std::uint64_t spent = r.spentSupplies + r.spentRepair + r.spentDues + r.spentRebuild;
        CHECK(earned - spent == r.finalCoin);
        CHECK(r.peakCoin >= r.finalCoin);
        CHECK(r.days == EconomySimTuning{}.days);
    }
}

TEST_CASE("a seed gives the same report, and other seeds differ")
{
    const CurrencyTable table = shipped();
    const EconomySimReport a = run(table, EconomySimTuning{}, 7);
    const EconomySimReport b = run(table, EconomySimTuning{}, 7);
    CHECK(a.digest == b.digest);
    CHECK(a.richPeriodDays == b.richPeriodDays);
    CHECK(a.finalCoin == b.finalCoin);

    std::uint32_t differing = 0;
    for (std::uint64_t seed = 8; seed < 18; ++seed) {
        differing += run(table, EconomySimTuning{}, seed).digest != a.digest ? 1u : 0u;
    }
    CHECK(differing == 10);
}

TEST_CASE("the target accepts brief spells and refuses too few, too many, too long or too wide")
{
    const EconomyTarget target;
    CHECK(meetsTarget(reportOf(1200, {3, 4}), target));
    CHECK(meetsTarget(reportOf(1200, {3, 4, 5}), target));
    CHECK_FALSE(meetsTarget(reportOf(1200, {}), target));
    CHECK_FALSE(meetsTarget(reportOf(1200, {4}), target));
    CHECK_FALSE(meetsTarget(reportOf(1200, {3, 3, 3, 3}), target));
    CHECK_FALSE(meetsTarget(reportOf(1200, {3, 400}), target));
    CHECK_FALSE(meetsTarget(reportOf(600, {14, 14, 14}), target));
    CHECK_FALSE(meetsTarget(reportOf(0, {}), target));
}

TEST_CASE("hostile tunings are refused with a reason")
{
    const CurrencyTable table = shipped();
    constexpr std::uint64_t kHuge = std::numeric_limits<std::uint64_t>::max();

    CHECK_FALSE(rejects(table, EconomySimTuning{}));

    SECTION("no days") { EconomySimTuning t; t.days = 0; CHECK(rejects(table, t)); }
    SECTION("too many days") { EconomySimTuning t; t.days = kMaxSimDays + 1; CHECK(rejects(table, t)); }
    SECTION("zero threshold") { EconomySimTuning t; t.richThreshold = 0; CHECK(rejects(table, t)); }
    SECTION("exit above entry") { EconomySimTuning t; t.richExitPermille = 1001; CHECK(rejects(table, t)); }
    SECTION("loot min above max") { EconomySimTuning t; t.lootMin = 9; t.lootMax = 8; CHECK(rejects(table, t)); }
    SECTION("huge loot") { EconomySimTuning t; t.lootMax = kHuge; CHECK(rejects(table, t)); }
    SECTION("huge hoard") { EconomySimTuning t; t.windfallRewardMax = kHuge; CHECK(rejects(table, t)); }
    SECTION("huge quest") { EconomySimTuning t; t.questRewardMax = kMaxGoodPrice + 1; CHECK(rejects(table, t)); }
    SECTION("chance above 1000") { EconomySimTuning t; t.lootChancePermille = 1001; CHECK(rejects(table, t)); }
    SECTION("zero sale interval") { EconomySimTuning t; t.saleIntervalDays = 0; CHECK(rejects(table, t)); }
    SECTION("sale units inverted") { EconomySimTuning t; t.saleUnitsMin = 7; CHECK(rejects(table, t)); }
    SECTION("zero quest interval") { EconomySimTuning t; t.questIntervalMin = 0; CHECK(rejects(table, t)); }
    SECTION("quest interval inverted") { EconomySimTuning t; t.questIntervalMin = 90; CHECK(rejects(table, t)); }
    SECTION("no hoards allowed in tiny campaigns") { EconomySimTuning t; t.days = 8; CHECK(rejects(table, t)); }
    SECTION("hoards inverted") { EconomySimTuning t; t.windfallMin = 4; CHECK(rejects(table, t)); }
    SECTION("too many rations") { EconomySimTuning t; t.rationsPerDay = 1000001; CHECK(rejects(table, t)); }
    SECTION("zero repair interval") { EconomySimTuning t; t.repairIntervalDays = 0; CHECK(rejects(table, t)); }
    SECTION("dues without a period") { EconomySimTuning t; t.duesPeriodDays = 0; CHECK(rejects(table, t)); }
    SECTION("share above 1000") { EconomySimTuning t; t.rebuildSharePermille = 1001; CHECK(rejects(table, t)); }
    SECTION("free project") { EconomySimTuning t; t.rebuildCosts = {0}; CHECK(rejects(table, t)); }
    SECTION("overpriced project") { EconomySimTuning t; t.rebuildCosts = {kMaxCurrencyCap + 1}; CHECK(rejects(table, t)); }
    SECTION("too many projects")
    {
        EconomySimTuning t;
        t.rebuildCosts.assign(kMaxSimRebuildProjects + 1, 100);
        CHECK(rejects(table, t));
    }
    SECTION("pricing rule above the limit") { EconomySimTuning t; t.pricing.scarcePrice = kMaxPermille + 1; CHECK(rejects(table, t)); }
    SECTION("no projects is allowed and leaves the supply route cut")
    {
        EconomySimTuning t;
        t.rebuildCosts.clear();
        CHECK_FALSE(rejects(table, t));
    }
}

TEST_CASE("a currency table that forbids a sink the sim uses is refused")
{
    const auto root = std::filesystem::path(ICHOR_TEST_OUTPUT) / ("econ-sim-" + std::to_string(ICHOR_PID));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "currency");
    std::ofstream(root / "currency" / "currencies.json", std::ios::binary)
        << R"([{"id": "coin", "name": "Coin", "symbol": "c", "earn": ["loot", "quest", "trade"], "spend": ["trade", "repair", "dues"], "cap": 1000000},
               {"id": "hard", "name": "Charge", "symbol": "ch", "earn": ["loot"], "spend": ["power"], "cap": 50}])";
    DataSet data;
    REQUIRE(data.loadDirectory(root));
    const DataTable* raw = data.table("currency");
    REQUIRE(raw != nullptr);
    std::string error;
    auto table = CurrencyTable::fromTable(*raw, error);
    REQUIRE(table.has_value());

    std::string simError;
    CHECK_FALSE(runEconomySim(*table, EconomySimTuning{}, 1, simError).has_value());
    CHECK(simError.find("rebuild") != std::string::npos);
}

TEST_CASE("print the measured campaign numbers", "[.economy-sim-report]")
{
    const CurrencyTable table = shipped();
    std::uint64_t earned[4] = {};
    std::uint64_t spent[4] = {};
    std::uint64_t missed = 0;
    std::uint64_t peak = 0;
    std::uint64_t exhausted = 0;
    std::uint32_t histogram[6] = {};
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const EconomySimReport r = run(table, EconomySimTuning{}, seed);
        earned[0] += r.earnedLoot;
        earned[1] += r.earnedHoards;
        earned[2] += r.earnedQuest;
        earned[3] += r.earnedTrade;
        spent[0] += r.spentSupplies;
        spent[1] += r.spentRepair;
        spent[2] += r.spentDues;
        spent[3] += r.spentRebuild;
        missed += r.missedUpkeepDays;
        peak += r.peakCoin;
        exhausted += r.sinksExhaustedDay != 0 ? 1 : 0;
        ++histogram[std::min<std::size_t>(r.richPeriodDays.size(), 5)];
    }
    const EconomySweep result = sweep(table, EconomySimTuning{});
    WARN("seeds " << kSeeds << " hits " << result.hits << " periods " << result.fewestPeriods << ".." << result.mostPeriods << " mean x100 "
                  << result.totalPeriods * 100 / result.seeds << " longest spell " << result.longestPeriodDays << " rich days "
                  << result.totalRichDays << "/" << result.totalDays);
    WARN("periods histogram 0.." << "5+: " << histogram[0] << " " << histogram[1] << " " << histogram[2] << " " << histogram[3] << " " << histogram[4] << " " << histogram[5]);
    WARN("mean per campaign earned loot/hoards/quest/trade " << earned[0] / kSeeds << " " << earned[1] / kSeeds << " " << earned[2] / kSeeds << " " << earned[3] / kSeeds);
    WARN("mean per campaign spent supplies/repair/dues/rebuild " << spent[0] / kSeeds << " " << spent[1] / kSeeds << " " << spent[2] / kSeeds << " " << spent[3] / kSeeds);
    WARN("mean peak " << peak / kSeeds << " missed upkeep days " << missed / kSeeds << " seeds with all projects done " << exhausted);
}
