#include "ichor/factions/standing_display.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<Faction> kFactions = {
    {"archonate", "The Archonate", FactionRole::Incumbent},
    {"archaists", "The Archaists", FactionRole::Challenger},
    {"mnemones", "The Mnemones", FactionRole::Custodian},
};

const std::vector<StandingTier> kTiers = {
    {"hostile", "Hostile", -1000},
    {"wary", "Wary", -250},
    {"neutral", "Neutral", 0},
    {"friendly", "Friendly", 250},
    {"allied", "Allied", 750},
};

const std::vector<StandingGate> kGates = {
    {"safe_passage", "archonate", "wary"},
    {"stock_common", "archonate", "neutral"},
    {"trainer_basic", "archonate", "friendly"},
    {"trainer_master", "archonate", "allied"},
    {"stock_rare", "archonate", "friendly"},
    {"base_access", "mnemones", "allied"},
};

StandingDisplay show(double standing, std::string_view faction = "archonate")
{
    return buildStandingDisplay(faction, standing, kFactions, kTiers, kGates);
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

using Ids = std::vector<std::string>;

}

TEST_CASE("standing display picks the tier at each boundary", "[standing_display]")
{
    CHECK(show(-1000).tier.id == "hostile");
    CHECK(show(-251).tier.id == "hostile");
    CHECK(show(-250).tier.id == "wary");
    CHECK(show(-1).tier.id == "wary");
    CHECK(show(0).tier.id == "neutral");
    CHECK(show(249).tier.id == "neutral");
    CHECK(show(250).tier.id == "friendly");
    CHECK(show(749).tier.id == "friendly");
    CHECK(show(750).tier.id == "allied");
    CHECK(show(1000).tier.id == "allied");
    CHECK(show(250).tierIndex == 3);
    CHECK(show(250).tierCount == 5);
    CHECK(show(250).tier.name == "Friendly");
}

TEST_CASE("standing display reports progress to the next tier", "[standing_display]")
{
    const StandingDisplay start = show(250);
    CHECK(start.progress == 0.0);
    CHECK(start.pointsToNext == 500);
    CHECK(start.nextTier.id == "allied");
    CHECK(start.nextTier.threshold == 750);
    CHECK_FALSE(start.atTopTier);

    const StandingDisplay half = show(500);
    CHECK(half.progress == 0.5);
    CHECK(half.pointsToNext == 250);

    const StandingDisplay last = show(749);
    CHECK(last.pointsToNext == 1);
    CHECK(last.progress > 0.99);
    CHECK(last.progress < 1.0);

    const StandingDisplay negative = show(-125);
    CHECK(negative.tier.id == "wary");
    CHECK(negative.progress == 0.5);
    CHECK(negative.pointsToNext == 125);
}

TEST_CASE("standing display at the top tier has nothing further", "[standing_display]")
{
    const StandingDisplay top = show(900);
    CHECK(top.atTopTier);
    CHECK(top.progress == 1.0);
    CHECK(top.pointsToNext == 0);
    CHECK(top.nextTier.id.empty());
    CHECK(top.unlocksAtNextTier.empty());
}

TEST_CASE("standing display lists exactly what each tier unlocks", "[standing_display]")
{
    const StandingDisplay friendly = show(300);
    CHECK(friendly.unlocksAtTier == Ids{"stock_rare", "trainer_basic"});
    CHECK(friendly.unlockedNow == Ids{"safe_passage", "stock_common", "stock_rare", "trainer_basic"});
    CHECK(friendly.unlocksAtNextTier == Ids{"trainer_master"});

    const StandingDisplay wary = show(-100);
    CHECK(wary.unlocksAtTier == Ids{"safe_passage"});
    CHECK(wary.unlockedNow == Ids{"safe_passage"});
    CHECK(wary.unlocksAtNextTier == Ids{"stock_common"});
}

TEST_CASE("standing display unlocks only the faction's own gates", "[standing_display]")
{
    const StandingDisplay mnemones = show(800, "mnemones");
    CHECK(mnemones.unlocksAtTier == Ids{"base_access"});
    CHECK(mnemones.unlockedNow == Ids{"base_access"});

    const StandingDisplay lower = show(100, "mnemones");
    CHECK(lower.unlockedNow.empty());
    CHECK(lower.unlocksAtNextTier == Ids{});
}

TEST_CASE("standing display handles tiers that unlock nothing", "[standing_display]")
{
    const StandingDisplay hostile = show(-900);
    CHECK(hostile.unlocksAtTier.empty());
    CHECK(hostile.unlockedNow.empty());
    CHECK(hostile.unlocksAtNextTier == Ids{"safe_passage"});

    const StandingDisplay noGates = buildStandingDisplay("archonate", 300, kFactions, kTiers, {});
    CHECK(noGates.status == StandingDisplayStatus::Ok);
    CHECK(noGates.tier.id == "friendly");
    CHECK(noGates.unlocksAtTier.empty());
    CHECK(noGates.unlockedNow.empty());
    CHECK(noGates.unlocksAtNextTier.empty());
}

TEST_CASE("standing display clamps out of range standing", "[standing_display]")
{
    const StandingDisplay high = show(5000.0);
    CHECK(high.status == StandingDisplayStatus::Ok);
    CHECK(high.standing == kStandingMax);
    CHECK(high.tier.id == "allied");

    const StandingDisplay low = show(-1.0e18);
    CHECK(low.standing == kStandingMin);
    CHECK(low.tier.id == "hostile");

    CHECK(show(1.0e300).standing == kStandingMax);
    CHECK(show(249.9).standing == 249);
    CHECK(show(-0.5).standing == 0);
}

TEST_CASE("standing display rejects NaN and infinite standing", "[standing_display]")
{
    for (const double bad : {kNaN, kInf, -kInf}) {
        const StandingDisplay display = show(bad);
        CHECK(display.status == StandingDisplayStatus::InvalidStanding);
        CHECK(display.faction == "archonate");
        CHECK(display.tier.id.empty());
        CHECK(display.unlockedNow.empty());
    }
}

TEST_CASE("standing display reports unknown factions", "[standing_display]")
{
    const StandingDisplay unknown = show(300, "nobody");
    CHECK(unknown.status == StandingDisplayStatus::UnknownFaction);
    CHECK(unknown.faction == "nobody");
    CHECK(unknown.tier.id.empty());
    CHECK(unknown.unlockedNow.empty());

    CHECK(show(300, "").status == StandingDisplayStatus::UnknownFaction);
    CHECK(show(300, "Archonate").status == StandingDisplayStatus::UnknownFaction);
}

TEST_CASE("standing display survives empty and hostile inputs", "[standing_display]")
{
    CHECK(buildStandingDisplay("archonate", 0, kFactions, {}, kGates).status == StandingDisplayStatus::NoTiers);
    CHECK(buildStandingDisplay("archonate", 0, {}, kTiers, kGates).status == StandingDisplayStatus::UnknownFaction);

    const std::vector<StandingGate> hostile = {
        {"ghost_tier", "archonate", "no_such_tier"},
        {"ghost_faction", "no_such_faction", "neutral"},
        {"dup", "archonate", "neutral"},
        {"dup", "archonate", "neutral"},
    };
    const StandingDisplay display = buildStandingDisplay("archonate", 100, kFactions, kTiers, hostile);
    CHECK(display.status == StandingDisplayStatus::Ok);
    CHECK(display.unlockedNow == Ids{"dup"});
    CHECK(display.unlocksAtTier == Ids{"dup"});
}

TEST_CASE("standing display agrees with FactionStanding gates", "[standing_display]")
{
    FactionStanding standing(kFactions, kTiers, kGates);
    for (const std::int32_t value : {-1000, -250, -1, 0, 249, 250, 749, 750, 1000}) {
        standing.change("archonate", value - *standing.standing("archonate"));
        const StandingDisplay display = show(value);
        CHECK(display.tier.id == standing.tier("archonate")->id);
        for (const StandingGate& gate : kGates) {
            if (gate.faction != "archonate") {
                continue;
            }
            const bool listed = std::find(display.unlockedNow.begin(), display.unlockedNow.end(), gate.id) != display.unlockedNow.end();
            CHECK(listed == standing.allowed(gate.id));
        }
    }
}
