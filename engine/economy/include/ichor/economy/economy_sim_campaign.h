#pragma once

#include "ichor/economy/merchant.h"
#include "ichor/economy/wallet.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// Doc 06 target: the player feels briefly rich two or three times per campaign,
// never continuously. This is a seeded, integer-only campaign over the real
// economy code (Wallet, merchant pricing, AccessFees dues, RebuildBoard) that
// measures how often and for how long the coin wallet is "rich". It reads no
// clock and uses no float, so a seed gives the same report on every platform.
//
// One tick is one day. Each day: income (loot, trade, quest, and a few hoards),
// then upkeep, repair and dues, then the day's rich sample, then the player
// invests a share of the surplus in the first open rebuild project.

constexpr std::uint32_t kMaxSimDays = 100000;
constexpr std::uint32_t kMaxSimRebuildProjects = 64;

struct EconomySimTuning {
    std::uint32_t days = 1200;

    // Rich: entered at richThreshold coins, left below richExitPermille of it.
    std::uint64_t richThreshold = 1500;
    std::uint32_t richExitPermille = 800;

    // Everyday loot: a coin find on lootChancePermille of days.
    std::uint32_t lootChancePermille = 500;
    std::uint64_t lootMin = 8;
    std::uint64_t lootMax = 32;

    // Salvage sold to the merchant every saleIntervalDays at the buyback price.
    std::uint32_t saleIntervalDays = 5;
    std::uint32_t saleUnitsMin = 3;
    std::uint32_t saleUnitsMax = 6;

    // A quest reward every questIntervalMin..Max days.
    std::uint32_t questIntervalMin = 40;
    std::uint32_t questIntervalMax = 80;
    std::uint64_t questRewardMin = 120;
    std::uint64_t questRewardMax = 320;

    // Hoards: windfallMin..windfallMax of them, one per equal slice of the
    // campaign, each in the middle half of its slice (so a long campaign keeps
    // two apart; a tiny one may not).
    std::uint32_t windfallMin = 2;
    std::uint32_t windfallMax = 3;
    std::uint64_t windfallRewardMin = 2200;
    std::uint64_t windfallRewardMax = 3200;

    // Sinks. Supplies are bought at the merchant's price, so a cut route costs more.
    std::uint32_t rationsPerDay = 1;
    std::uint32_t repairIntervalDays = 10;
    std::uint64_t repairCost = 40;
    std::uint64_t duesAmount = 60;
    std::uint64_t duesPeriodDays = 30;

    // Rebuild: the player funds the first open project with this share of the
    // coin above the reserve each day. The first project is a trade route
    // that starts cut, so funding it lowers the supply price.
    std::vector<std::uint64_t> rebuildCosts = {500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
    std::uint64_t rebuildReserve = 300;
    std::uint32_t rebuildSharePermille = 200;

    PricingRules pricing;
};

struct EconomyTarget {
    std::uint32_t minPeriods = 2;
    std::uint32_t maxPeriods = 3;
    // "Briefly": no single rich spell lasts longer than this many days...
    std::uint32_t maxPeriodDays = 14;
    // ...and together they cover no more than this share of the campaign.
    std::uint32_t maxRichPermille = 50;
};

struct EconomySimReport {
    std::uint32_t days = 0;
    std::uint32_t richDays = 0;
    std::vector<std::uint32_t> richPeriodDays; // one entry per rich spell, in order
    std::uint64_t peakCoin = 0;
    std::uint64_t finalCoin = 0;

    std::uint64_t earnedLoot = 0;
    std::uint64_t earnedHoards = 0;
    std::uint64_t earnedQuest = 0;
    std::uint64_t earnedTrade = 0;
    std::uint64_t spentSupplies = 0;
    std::uint64_t spentRepair = 0;
    std::uint64_t spentDues = 0;
    std::uint64_t spentRebuild = 0;

    std::uint32_t missedUpkeepDays = 0;  // days the wallet could not buy supplies
    std::uint32_t sinksExhaustedDay = 0; // first day every rebuild project was done; 0 if never
    std::uint64_t digest = 0;
};

// Nothing (with `error` set) for a tuning with a zero or oversize day count, a
// min above its max, a share above 1000, a project cost of zero or above
// kMaxCurrencyCap, too many projects, a zero interval, or an amount above
// kMaxGoodPrice, or for a currency table that forbids a sink the sim uses.
std::optional<EconomySimReport> runEconomySim(const CurrencyTable& table, const EconomySimTuning& tuning, std::uint64_t seed, std::string& error);

bool meetsTarget(const EconomySimReport& report, const EconomyTarget& target);

struct EconomySweep {
    std::uint32_t seeds = 0;
    std::uint32_t hits = 0; // seeds that meet the target
    std::uint64_t totalPeriods = 0;
    std::uint32_t fewestPeriods = 0;
    std::uint32_t mostPeriods = 0;
    std::uint32_t longestPeriodDays = 0;
    std::uint64_t totalRichDays = 0;
    std::uint64_t totalDays = 0;
    std::uint32_t seedsWithLongSpell = 0;
};

// Runs seeds firstSeed .. firstSeed + count - 1.
std::optional<EconomySweep> sweepEconomySim(const CurrencyTable& table, const EconomySimTuning& tuning, const EconomyTarget& target,
    std::uint64_t firstSeed, std::uint32_t count, std::string& error);

}
