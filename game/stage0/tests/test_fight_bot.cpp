#include "ichor/stage0/fight_bot.h"
#include "ichor/stage0/fight_report.h"
#include "../../../engine/tests/test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace ichor::stage0;

namespace {

BotConfig config(const char* skill, std::uint64_t seed, int fights)
{
    BotConfig c;
    c.skill = *findBotSkill(skill);
    c.seed = seed;
    c.fights = fights;
    return c;
}

}

TEST_CASE("the same seed and skill give the same fights")
{
    const BotResult a = runBotFights(config("regular", 5, 12));
    const BotResult b = runBotFights(config("regular", 5, 12));
    REQUIRE(a.rows.size() == b.rows.size());
    REQUIRE(!a.rows.empty());
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        REQUIRE(a.rows[i].seconds == b.rows[i].seconds);
        REQUIRE(a.rows[i].playerLost == b.rows[i].playerLost);
        REQUIRE(a.rows[i].cause == b.rows[i].cause);
        REQUIRE(a.rows[i].playerHits == b.rows[i].playerHits);
    }
    REQUIRE(formatBotResult(config("regular", 5, 12), a) == formatBotResult(config("regular", 5, 12), b));
}

TEST_CASE("a different seed plays different fights")
{
    const BotResult a = runBotFights(config("regular", 5, 20));
    const BotResult b = runBotFights(config("regular", 6, 20));
    bool differs = a.rows.size() != b.rows.size();
    for (std::size_t i = 0; !differs && i < a.rows.size(); ++i) {
        differs = a.rows[i].seconds != b.rows[i].seconds || a.rows[i].playerDamageTaken != b.rows[i].playerDamageTaken;
    }
    REQUIRE(differs);
}

TEST_CASE("a stronger skill level wins clearly more fights")
{
    // Summed over seeds so one lucky stream cannot decide it. If the bot ignored its skill parameters,
    // both totals would be equal and the margin below would fail.
    int novice = 0;
    int regular = 0;
    int expert = 0;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        novice += runBotFights(config("novice", seed, 40)).summary.playerWins;
        regular += runBotFights(config("regular", seed, 40)).summary.playerWins;
        expert += runBotFights(config("expert", seed, 40)).summary.playerWins;
    }
    REQUIRE(regular > novice + 10);
    REQUIRE(expert > regular + 10);
}

TEST_CASE("each skill parameter changes the outcome on its own")
{
    BotConfig slow = config("expert", 2, 60);
    slow.skill.reactionTicks = 40;
    REQUIRE(runBotFights(slow).summary.playerWins + 10 < runBotFights(config("expert", 2, 60)).summary.playerWins);

    BotConfig bare = config("expert", 2, 60);
    bare.skill.blockChance = 0.0f;
    bare.skill.dodgeChance = 0.0f;
    REQUIRE(runBotFights(bare).summary.playerWins + 5 < runBotFights(config("expert", 2, 60)).summary.playerWins);

    BotConfig timid = config("expert", 2, 60);
    timid.skill.aggression = 0.0f;
    REQUIRE(runBotFights(timid).summary.playerWins + 5 < runBotFights(config("expert", 2, 60)).summary.playerWins);
}

TEST_CASE("golden summary for a fixed seed")
{
    const BotConfig c = config("regular", 7, 30);
    const BotResult result = runBotFights(c);
    const std::string expected =
        "bot regular (reaction 14 ticks, block 0.30, dodge 0.20, aggression 0.10), seed 7, 30 fights played, 0 timed out\n"
        "fights 30 (won 22, lost 8), median 9.2s, median retry gap 0.0s\n"
        "  whiff_punished    2   25.0%\n"
        "  cornered          1   12.5%\n"
        "  mistimed_block    1   12.5%\n"
        "  unreadable        4   50.0%\n"
        "diagnosis: too few deaths\n"
        "note: bot results are a rough early signal, not a substitute for the 200-fight protocol.\n";
    REQUIRE(formatBotResult(c, result) == expected);
}

TEST_CASE("the fights land in the csv the report tool reads")
{
    const std::filesystem::path csv = ichor::test::tempPath("fight_bot.csv");
    std::filesystem::remove(csv);
    BotConfig c = config("regular", 3, 8);
    c.csv = csv;
    const BotResult result = runBotFights(c);

    std::ifstream in(csv, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::vector<FightRow> rows = parseFightLog(text);
    REQUIRE(rows.size() == result.rows.size());
    REQUIRE(summarise(rows).playerWins == result.summary.playerWins);
    REQUIRE(summarise(rows).playerDeaths.total() == result.summary.playerDeaths.total());

    std::ifstream ledger(std::filesystem::path(csv).replace_extension(".sessions.csv"), std::ios::binary);
    const std::string ledgerText((std::istreambuf_iterator<char>(ledger)), std::istreambuf_iterator<char>());
    REQUIRE(ledgerText.find("bot-regular") != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove(csv, ignored);
    std::filesystem::remove(std::filesystem::path(csv).replace_extension(".sessions.csv"), ignored);
    std::filesystem::remove_all(std::filesystem::path(csv).replace_extension(".fights"), ignored);
}

TEST_CASE("hostile skill and fight counts are clamped, never crash")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const BotSkill clean = sanitise({"x", -5, nan, inf, -inf});
    REQUIRE(clean.reactionTicks == 0);
    REQUIRE(clean.blockChance == 0.0f);
    REQUIRE(clean.dodgeChance == 0.0f);
    REQUIRE(clean.aggression == 0.5f);
    REQUIRE(sanitise({"x", 100000, 7.0f, -1.0f, 2.0f}).reactionTicks == kBotHistoryTicks - 1);
    REQUIRE(sanitise({"x", 1, 7.0f, -1.0f, 2.0f}).blockChance == 1.0f);

    BotConfig c = config("regular", 1, -3);
    REQUIRE(runBotFights(c).rows.empty());
    c.fights = 2;
    c.maxFightTicks = -1;
    c.skill = {"x", 1 << 30, nan, nan, nan};
    const BotResult result = runBotFights(c);
    REQUIRE(result.timeouts + static_cast<int>(result.rows.size()) == 2);
}

TEST_CASE("a fight that never ends times out and is not logged")
{
    BotConfig c = config("expert", 1, 3);
    c.maxFightTicks = 5;
    const BotResult result = runBotFights(c);
    REQUIRE(result.timeouts == 3);
    REQUIRE(result.rows.empty());
}

TEST_CASE("skill names resolve and unknown names do not")
{
    REQUIRE(findBotSkill("novice").has_value());
    REQUIRE(findBotSkill("expert")->reactionTicks < findBotSkill("novice")->reactionTicks);
    REQUIRE(!findBotSkill("").has_value());
    REQUIRE(!findBotSkill("god").has_value());
}
